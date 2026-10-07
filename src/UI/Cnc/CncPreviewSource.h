/*
 * CncPreviewSource.h
 *
 * The job file for JOB > PREVIEW: downloaded and read in the background.
 *
 * The file comes over the network whatever the machine connection is (USB is the recommended one,
 * and it cannot carry file contents):
 *  - NETWORK connection: DuetScreen's own download (its session, standalone or SBC).
 *  - USB / UART: a separate HTTP download from the address the machine reports (network.interfaces
 *    actualIP): rr_connect (password from Panel::DuetPassword, RRF's default "reprap"), rr_download,
 *    rr_disconnect. DSF in SBC mode answers the same requests.
 * No address: NeedsNetwork; the machine connection is never touched.
 *
 * The page polls Generation() and reads Current() when it changed. One file at a time: asking for
 * another one drops the result of the one before.
 */
#pragma once

#include "CncGcode.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace UI::Cnc::PreviewSource
{
	enum class Status
	{
		NoFile,			// nothing asked for
		NeedsNetwork,	// no network address of the machine
		Loading,		// downloading / reading
		Ready,
		Failed,			// download failed (detail in Current().message)
		TooLarge,
		Empty			// no moves in the file
	};

	struct Result
	{
		Status status = Status::NoFile;
		std::string path;							// "0:/gcodes/part.nc"
		std::string message;						// Failed: why
		bool passwordRejected = false;				// Failed: the machine wants another password
		std::shared_ptr<const Gcode::Program> program;	// Ready
	};

	void Request(const std::string& path);			// same path loaded or loading: nothing happens
	void Reload();									// the same file again (after a failure)
	Result Current();
	uint32_t Generation();							// changes whenever Current() changes

	void SetMachineIp(const std::string& ip);		// from the object model
	std::string MachineIp();

	constexpr size_t MaxFileSize = 32u * 1024u * 1024u;
	constexpr const char* WrongPassword = "wrong machine password";
	constexpr int HttpTimeoutSeconds = 30;

	// Tests: the file contents come from here, at once (no thread, no network). Return false: download failed
	// (contents, when set, is the reason: WrongPassword for a rejected password).
	using TestSource = std::function<bool(const std::string& path, std::string& contents)>;
	void SetTestSource(TestSource source);
	void Reset();
}
