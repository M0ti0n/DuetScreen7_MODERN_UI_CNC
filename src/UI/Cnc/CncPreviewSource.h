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
 * The page polls Generation() and reads Current() when it changed. One file is current; the file
 * shown before it is kept (when not too large, KeepMaxPoints), so going back to it (a running job and
 * the next one) needs no new download.
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

	// What JOB LIST knows about a file (size, date): a changed file (uploaded again under the same name) is
	// read again; a file over MaxFileSize is refused before any download. Empty: not known.
	struct Stamp
	{
		size_t size = 0;
		std::string date;
		bool known() const { return size > 0 || !date.empty(); }
		bool operator==(const Stamp&) const = default;
	};

	struct Result
	{
		Status status = Status::NoFile;
		std::string path;							// "0:/gcodes/part.nc"
		std::string message;						// Failed: why
		bool passwordRejected = false;				// Failed: the machine wants another password
		std::shared_ptr<const Gcode::Program> program;	// Ready
		Stamp stamp;									// the file's, when known
	};

	// Same path loaded or loading: nothing happens, unless the stamps differ (both known). A changed SPLIT
	// OPERATIONS: CurrentIsStale(), the page calls Reload() when a job allows it.
	void Request(const std::string& path, const Stamp& stamp = Stamp());
	void Reload();									// the same file again (after a failure)
	bool Has(const std::string& path);				// read, being read or kept: Request needs no new download
	void Forget(const std::string& path);			// the file was deleted (or changed): drop what is kept
	Result Current();
	uint32_t Generation();							// changes whenever Current() changes

	void SetMachineIp(const std::string& ip);		// from the object model
	std::string MachineIp();

	constexpr size_t MaxFileSize = 32u * 1024u * 1024u;
	constexpr const char* WrongPassword = "wrong machine password";
	constexpr int HttpTimeoutSeconds = 30;			   // connect; the download: plus 1 s per MinBytesPerSecond
	constexpr size_t MinBytesPerSecond = 100u * 1024u; // a slow Duet 2 WiFi still makes it
	constexpr int UnknownSizeTimeoutSeconds = 120;
	bool CurrentIsStale();					  // read with another SPLIT OPERATIONS setting
	constexpr size_t KeepMaxPoints = 1000000; // the file before is kept up to this size (16 MB of points)

	// Tests: the file contents come from here, at once (no thread, no network). Return false: download failed
	// (contents, when set, is the reason: WrongPassword for a rejected password).
	using TestSource = std::function<bool(const std::string& path, std::string& contents)>;
	void SetTestSource(TestSource source);
	void Reset();
}
