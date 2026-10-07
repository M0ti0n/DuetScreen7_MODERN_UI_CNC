/*
 * CncPreviewSource.cpp
 */

#include "CncPreviewSource.h"
#include "CncPanel.h"
#include "Debug.h"
#include <atomic>
#include <hv/requests.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace UI::Cnc::PreviewSource
{
	namespace
	{
		std::mutex s_mutex;
		Result s_result;
		std::atomic<uint32_t> s_generation{0};
		uint64_t s_requestId = 0;
		std::string s_ip;
		TestSource s_test;

		// Only the newest request may set the result
		void publish(uint64_t id, Result r)
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			if (id != s_requestId)
			{
				return;
			}
			s_result = std::move(r);
			++s_generation;
		}

		Result read(const std::string& path, const std::string& contents, const Gcode::Options& options)
		{
			Result r;
			r.path = path;
			if (contents.size() > MaxFileSize)
			{
				r.status = Status::TooLarge;
				return r;
			}
			auto program = std::make_shared<Gcode::Program>(Gcode::Parse(contents, options));
			r.status = program->ops.empty() ? Status::Empty : Status::Ready;
			r.program = std::move(program);
			return r;
		}

		bool usableIp(const std::string& ip)
		{
			return !ip.empty() && ip != "0.0.0.0";
		}

		std::string baseUrl(const std::string& ip)
		{
			return (ip.rfind("http://", 0) == 0 || ip.rfind("https://", 0) == 0) ? ip : "http://" + ip;
		}

		// rr_connect, rr_download, rr_disconnect (RRF standalone, DSF in SBC mode)
		bool download(const std::string& ip,
					  const std::string& password,
					  const std::string& path,
					  std::string& body,
					  std::string& error)
		{
			hv::HttpClient cli;
			cli.setTimeout(HttpTimeoutSeconds);
			const std::string base = baseUrl(ip);

			HttpRequest connect;
			connect.method = HTTP_GET;
			connect.url = base + "/rr_connect";
			connect.query_params["password"] = password;
			connect.timeout = 10;
			HttpResponse connectResp;
			if (cli.send(&connect, &connectResp) != 0 || connectResp.status_code != HTTP_STATUS_OK)
			{
				error = "no answer from " + ip;
				return false;
			}
			std::string sessionKey;
			const nlohmann::json j = nlohmann::json::parse(connectResp.body, nullptr, false);
			if (j.is_object())
			{
				const int err = j.value("err", 0);
				if (err == 1)
				{
					error = WrongPassword;
					return false;
				}
				if (err == 2)
				{
					error = "the machine has no free session";
					return false;
				}
				if (j.contains("sessionKey") && j["sessionKey"].is_number())
				{
					sessionKey = std::to_string(j["sessionKey"].get<long long>());
				}
			}

			HttpRequest get;
			get.method = HTTP_GET;
			get.url = base + "/rr_download";
			get.query_params["name"] = path;
			if (!sessionKey.empty())
			{
				get.headers["X-Session-Key"] = sessionKey;
			}
			get.timeout = HttpTimeoutSeconds;
			HttpResponse getResp;
			const bool ok = cli.send(&get, &getResp) == 0 && getResp.status_code == HTTP_STATUS_OK;
			if (ok)
			{
				body = std::move(getResp.body);
			}
			else
			{
				error = (getResp.status_code == HTTP_STATUS_NOT_FOUND) ? std::string("file not found")
																	   : std::string("download failed");
			}

			HttpRequest bye;
			bye.method = HTTP_GET;
			bye.url = base + "/rr_disconnect";
			if (!sessionKey.empty())
			{
				bye.headers["X-Session-Key"] = sessionKey;
			}
			bye.timeout = 5;
			HttpResponse byeResp;
			cli.send(&bye, &byeResp);
			return ok;
		}

		void start(const std::string& path)
		{
			uint64_t id;
			{
				std::lock_guard<std::mutex> lock(s_mutex);
				id = ++s_requestId;
				s_result = Result();
				s_result.path = path;
				s_result.status = path.empty() ? Status::NoFile : Status::Loading;
				++s_generation;
			}
			if (path.empty())
			{
				return;
			}
			Gcode::Options options;
			options.toolChangesOnly = Panel::PreviewToolChangesOnly();

			if (s_test)
			{
				std::string contents;
				if (s_test(path, contents))
				{
					publish(id, read(path, contents, options));
				}
				else
				{
					Result r;
					r.path = path;
					r.status = Status::Failed;
					r.message = contents.empty() ? std::string("download failed") : contents; // the test's reason
					r.passwordRejected = r.message == WrongPassword;
					publish(id, r);
				}
				return;
			}

			// The machine's address: the one DuetScreen connects to over the network, else the one it reports
			const std::string ip = (Panel::Connection() == Panel::Link::Network) ? Panel::DuetIp() : MachineIp();
			if (!usableIp(ip))
			{
				Result r;
				r.path = path;
				r.status = Status::NeedsNetwork;
				publish(id, r);
				return;
			}
			const std::string password = Panel::DuetPassword();
			std::thread(
				[id, path, ip, password, options]
				{
					std::string body, error;
					if (download(ip, password, path, body, error))
					{
						publish(id, read(path, body, options));
					}
					else
					{
						LOG_WARN("PREVIEW: {:s}: {:s}", path, error);
						Result r;
						r.path = path;
						r.status = Status::Failed;
						r.message = error;
						r.passwordRejected = error == WrongPassword;
						publish(id, r);
					}
				})
				.detach();
		}
	} // namespace

	void Request(const std::string& path)
	{
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			if (path == s_result.path && s_result.status != Status::Failed && s_result.status != Status::NeedsNetwork)
			{
				return; // loaded or loading
			}
		}
		start(path);
	}

	void Reload()
	{
		std::string path;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			path = s_result.path;
		}
		start(path);
	}

	Result Current()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		return s_result;
	}

	uint32_t Generation()
	{
		return s_generation.load();
	}

	void SetMachineIp(const std::string& ip)
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_ip = ip;
	}

	std::string MachineIp()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		return s_ip;
	}

	void SetTestSource(TestSource source)
	{
		s_test = std::move(source);
	}

	void Reset()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		++s_requestId; // a download still running is ignored
		s_result = Result();
		s_ip.clear();
		++s_generation;
	}
} // namespace UI::Cnc::PreviewSource
