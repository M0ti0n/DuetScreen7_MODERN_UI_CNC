/*
 * CncPreviewSource.cpp
 */

#include "CncPreviewSource.h"
#include "CncPanel.h"
#include "Debug.h"
#include <algorithm>
#include <atomic>
#include <chrono>
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
		Result s_kept;					// the file shown before (Ready), else empty
		bool s_keptToolChanges = false; // read with SPLIT OPERATIONS: TOOL CHANGES
		bool s_resultToolChanges = false;
		std::atomic<uint32_t> s_generation{0};
		uint64_t s_requestId = 0;
		std::string s_ip;
		TestSource s_test;

		// The current result is replaced: keep it when it is a drawn file (s_mutex held)
		void keepCurrent()
		{
			if (s_result.status == Status::Ready && s_result.program &&
				s_result.program->points.size() <= KeepMaxPoints)
			{
				s_kept = s_result;
				s_keptToolChanges = s_resultToolChanges;
			}
		}

		// Only the newest request may set the result
		void publish(uint64_t id, Result r)
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			if (id != s_requestId)
			{
				return;
			}
			if (!r.stamp.known())
			{
				r.stamp = s_result.stamp; // learned while it was being read (same request: same file)
			}
			s_result = std::move(r);
			++s_generation;
		}

		Result read(const std::string& path,
					const Stamp& stamp,
					const std::string& contents,
					const Gcode::Options& options)
		{
			Result r;
			r.path = path;
			r.stamp = stamp;
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
					  int timeoutSeconds,
					  std::string& body,
					  std::string& error)
		{
			hv::HttpClient cli;
			cli.setTimeout(timeoutSeconds);
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
			get.timeout = (uint16_t)std::clamp(timeoutSeconds, 1, 3600);
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

		void start(const std::string& path, const Stamp& stamp)
		{
			uint64_t id;
			const bool toolChanges = Panel::PreviewToolChangesOnly();
			{
				std::lock_guard<std::mutex> lock(s_mutex);
				id = ++s_requestId;
				if (s_result.path != path)
				{
					keepCurrent();
				}
				if (s_kept.path == path)
				{
					s_kept = Result(); // read again now
				}
				s_result = Result();
				s_resultToolChanges = toolChanges;
				s_result.path = path;
				s_result.stamp = stamp;
				s_result.status = path.empty()				   ? Status::NoFile
								  : (stamp.size > MaxFileSize) ? Status::TooLarge // known from the listing
															   : Status::Loading;
				++s_generation;
				if (s_result.status != Status::Loading)
				{
					return; // nothing to download
				}
			}
			Gcode::Options options;
			options.toolChangesOnly = toolChanges;

			if (s_test)
			{
				std::string contents;
				if (s_test(path, contents))
				{
					publish(id, read(path, stamp, contents, options));
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
			const int timeout =
				stamp.known() ? HttpTimeoutSeconds + (int)(stamp.size / MinBytesPerSecond) : UnknownSizeTimeoutSeconds;
			std::thread(
				[id, path, stamp, ip, password, options, timeout]
				{
					std::string body, error;
					const auto t0 = std::chrono::steady_clock::now();
					if (download(ip, password, path, timeout, body, error))
					{
						const auto t1 = std::chrono::steady_clock::now();
						Result r = read(path, stamp, body, options);
						const auto t2 = std::chrono::steady_clock::now();
						// for checking on the panel: how long the machine took to send it, and the screen to read it
						LOG_INFO("PREVIEW: {:s}: {:d} bytes downloaded in {:d} ms, read in {:d} ms ({:d} points)",
								 path,
								 body.size(),
								 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
								 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count(),
								 r.program ? r.program->points.size() : (size_t)0);
						publish(id, std::move(r));
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

	void Request(const std::string& path, const Stamp& stamp)
	{
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			const bool toolChanges = Panel::PreviewToolChangesOnly();
			auto changed = [&stamp](const Stamp& held) { return stamp.known() && held.known() && !(stamp == held); };
			// (a changed SPLIT OPERATIONS is not a reason here: the page reloads for it when a job allows)
			if (path == s_result.path && s_result.status != Status::Failed && s_result.status != Status::NeedsNetwork &&
				!changed(s_result.stamp))
			{
				if (stamp.known() && !s_result.stamp.known())
				{
					s_result.stamp = stamp; // compared from now on
				}
				return; // loaded or loading
			}
			if (path == s_kept.path && changed(s_kept.stamp))
			{
				s_kept = Result(); // that file changed: read again
			}
			if (!path.empty() && path == s_kept.path && s_keptToolChanges == toolChanges)
			{
				// The file shown before: back at once, and the current one is kept instead
				++s_requestId; // a download still running is ignored
				Result back = std::move(s_kept);
				const bool backToolChanges = s_keptToolChanges;
				s_kept = Result();
				keepCurrent();
				s_result = std::move(back);
				s_resultToolChanges = backToolChanges;
				++s_generation;
				return;
			}
		}
		start(path, stamp);
	}

	bool CurrentIsStale()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		return !s_result.path.empty() && s_result.status != Status::NoFile &&
			   s_resultToolChanges != Panel::PreviewToolChangesOnly();
	}

	bool Has(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (path.empty())
		{
			return false;
		}
		if (path == s_result.path && s_result.status != Status::Failed && s_result.status != Status::NeedsNetwork &&
			s_result.status != Status::NoFile)
		{
			return true;
		}
		return path == s_kept.path && s_keptToolChanges == Panel::PreviewToolChangesOnly();
	}

	void Forget(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_kept.path == path)
		{
			s_kept = Result();
		}
		if (s_result.path == path)
		{
			++s_requestId; // a download still running is ignored
			s_result = Result();
			++s_generation;
		}
	}

	void Reload()
	{
		std::string path;
		Stamp stamp;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			path = s_result.path;
			stamp = s_result.stamp;
		}
		start(path, stamp);
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
		s_kept = Result();
		s_ip.clear();
		++s_generation;
	}
} // namespace UI::Cnc::PreviewSource
