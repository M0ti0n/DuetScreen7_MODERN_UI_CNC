/*
 * Communication.h
 *
 *  Created on: Jan 30, 2023
 *      Author: Loïc G.
 */

#ifndef JNI_COMM_COMMUNICATION_HPP_
#define JNI_COMM_COMMUNICATION_HPP_

#include "Comm/Commands.h"
#include "FileInfo.h"
#include <initializer_list>
#include <stdint.h>

namespace Comm
{

	enum SeqState
	{
		SeqStateInit,
		SeqStateRequested,
		SeqStateOk,
		SeqStateUpdate,
		SeqStateError,
		SeqStateDisabled
	};

	struct Seq
	{
		const ReceivedDataEvent seqid;

		int32_t lastSeq;
		enum SeqState state;
		std::chrono::milliseconds lastRequestTime = 0ms;

		const char* const key;
		const char* const flags;
	};

	extern Seq* g_currentReqSeq;

	bool GetInteger(const char s[], int32_t& rslt);
	bool GetUnsignedInteger(const char s[], uint32_t& rslt);
	bool GetFloat(const char s[], float& rslt);
	bool GetBool(const char s[], bool& rslt);
	void Reconnect();

	Seq* FindSeqByKey(const char* key);
	void UpdateSeq(const ReceivedDataEvent seqid, int32_t val);
	void ResetSeqs();

	void KickWatchdog();

	Seq* GetNextSeq(struct Seq* current);

	// The globals are read one by one (M409 K"global.<name>"): with MillenniumOS NeXT installed the whole global
	// object is too big for the machine's reply buffers ({"err":-1}). The CNC pages register the names they need;
	// one seqs.global update = one sweep over them. Without names the whole object is asked for.
	void AddGlobalKeys(std::initializer_list<const char*> names);
	bool IsGlobalSeq(const Seq* seq);
	void GlobalAnswered(Seq* seq);						// a reply to the sweep's current name is complete
	bool sendNext();
	void init();
} // namespace Comm

#endif /* JNI_COMM_COMMUNICATION_HPP_ */
