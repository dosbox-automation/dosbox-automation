// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/network/ne2000.h"

#include <array>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

namespace {

// Page registers are multiplied by 256 and used as an offset into
// s.mem[], so a page outside chip memory becomes an out-of-bounds heap
// access (the guest-to-host escape class of QEMU CVE-2016-2841).
constexpr uint8_t kFirstPage    = BX_NE2K_MEMSTART / 256;
constexpr uint8_t kLastPage     = (BX_NE2K_MEMEND / 256) - 1;
constexpr uint8_t kLastStopPage = BX_NE2K_MEMEND / 256;

constexpr io_port_t kPstartOffset = 0x1;
constexpr io_port_t kPstopOffset  = 0x2;
constexpr io_port_t kBnryOffset   = 0x3;
constexpr io_port_t kTpsrOffset   = 0x4;
constexpr io_port_t kCurrOffset   = 0x7;

// Below kFirstPage and above kLastPage respectively, and the two values
// a guest reaches for first.
constexpr uint8_t kPageBelowMemory = 0x00;
constexpr uint8_t kPageAboveMemory = 0xff;

constexpr unsigned kMinFrameBytes = 60;

class Ne2000Test : public ::testing::Test {
protected:
	void SetUp() override
	{
		nic = std::make_unique<bx_ne2k_c>();
	}

	// A receive ring spanning all of chip memory with one packet's worth
	// of headroom, promiscuous so address filtering never masks a result,
	// and receive interrupts off so no PIC is needed.
	void SetUpValidRing()
	{
		nic->s.CR.stop      = 0;
		nic->s.page_start   = kFirstPage;
		nic->s.page_stop    = kLastStopPage;
		nic->s.bound_ptr    = kFirstPage;
		nic->s.curr_page    = kFirstPage + 1;
		nic->s.RCR.promisc  = 1;
		nic->s.IMR.rx_inte  = 0;
	}

	// Unicast, so the multicast status bit stays clear and the header
	// contents below are fully determined.
	static std::array<uint8_t, kMinFrameBytes> MakeFrame()
	{
		std::array<uint8_t, kMinFrameBytes> frame = {};
		frame[0] = 0x02;
		return frame;
	}

	std::unique_ptr<bx_ne2k_c> nic;
};

TEST_F(Ne2000Test, PstartRejectsAPageBelowChipMemory)
{
	nic->s.page_start = kFirstPage;
	nic->page0_write(kPstartOffset, kPageBelowMemory, io_width_t::byte);
	EXPECT_EQ(nic->s.page_start, kFirstPage);
}

TEST_F(Ne2000Test, PstartRejectsAPageAboveChipMemory)
{
	nic->s.page_start = kFirstPage;
	nic->page0_write(kPstartOffset, kPageAboveMemory, io_width_t::byte);
	EXPECT_EQ(nic->s.page_start, kFirstPage);
}

TEST_F(Ne2000Test, PstartAcceptsBothEndsOfChipMemory)
{
	nic->page0_write(kPstartOffset, kFirstPage, io_width_t::byte);
	EXPECT_EQ(nic->s.page_start, kFirstPage);

	nic->page0_write(kPstartOffset, kLastPage, io_width_t::byte);
	EXPECT_EQ(nic->s.page_start, kLastPage);
}

// PSTOP names the page one past the end of the ring, so unlike every
// other page register it may legitimately equal MEMEND/256.
TEST_F(Ne2000Test, PstopAcceptsTheExclusiveEndPage)
{
	nic->page0_write(kPstopOffset, kLastStopPage, io_width_t::byte);
	EXPECT_EQ(nic->s.page_stop, kLastStopPage);
}

TEST_F(Ne2000Test, PstopRejectsThePageAfterTheExclusiveEnd)
{
	nic->s.page_stop = kLastStopPage;
	nic->page0_write(kPstopOffset, kLastStopPage + 1, io_width_t::byte);
	EXPECT_EQ(nic->s.page_stop, kLastStopPage);
}

TEST_F(Ne2000Test, BnryRejectsAPageOutsideChipMemory)
{
	nic->s.bound_ptr = kFirstPage;
	nic->page0_write(kBnryOffset, kPageAboveMemory, io_width_t::byte);
	EXPECT_EQ(nic->s.bound_ptr, kFirstPage);
}

TEST_F(Ne2000Test, TpsrRejectsAPageOutsideChipMemory)
{
	nic->s.tx_page_start = kFirstPage;
	nic->page0_write(kTpsrOffset, kPageAboveMemory, io_width_t::byte);
	EXPECT_EQ(nic->s.tx_page_start, kFirstPage);
}

TEST_F(Ne2000Test, CurrRejectsAPageOutsideChipMemory)
{
	nic->s.curr_page = kFirstPage;
	nic->page1_write(kCurrOffset, kPageAboveMemory, io_width_t::byte);
	EXPECT_EQ(nic->s.curr_page, kFirstPage);
}

TEST_F(Ne2000Test, CurrAcceptsAPageInsideChipMemory)
{
	nic->page1_write(kCurrOffset, kLastPage, io_width_t::byte);
	EXPECT_EQ(nic->s.curr_page, kLastPage);
}

// Drivers commonly program page 0 with outw, which the handler splits
// into two byte writes; the split path must be guarded as well.
TEST_F(Ne2000Test, AWordWriteCannotSmuggleAPagePastTheGuard)
{
	nic->s.page_start = kFirstPage;
	nic->s.page_stop  = kLastStopPage;

	nic->page0_write(kPstartOffset, 0xffff, io_width_t::word);

	EXPECT_EQ(nic->s.page_start, kFirstPage);
	EXPECT_EQ(nic->s.page_stop, kLastStopPage);
}

// Positive control: the refusals below only mean something if the same
// path demonstrably accepts a well-formed ring.
TEST_F(Ne2000Test, RxFrameAcceptsAWellFormedRing)
{
	SetUpValidRing();
	const auto frame = MakeFrame();

	const auto received = nic->rx_frame(frame.data(), kMinFrameBytes);

	EXPECT_EQ(received, static_cast<int>(kMinFrameBytes));
	EXPECT_EQ(nic->s.curr_page, kFirstPage + 2);

	// Header written at the old CURR page: status, next page, length.
	const size_t offset = (kFirstPage + 1) * 256 - BX_NE2K_MEMSTART;
	EXPECT_EQ(nic->s.mem[offset], 1);
	EXPECT_EQ(nic->s.mem[offset + 1], kFirstPage + 2);
	EXPECT_EQ(nic->s.mem[offset + 2], kMinFrameBytes + 4);
	EXPECT_EQ(nic->s.mem[offset + 3], 0);
}

// The register guards cannot see state set through any other path, so
// rx_frame re-checks the ring before it derives a mem[] address from it.
// Each ring below is one the guest could program: it clears the
// avail-space check (so the pre-guard code reaches the copy) and then
// makes CURR*256 land outside chip memory (GHSA-8278-xcrf-28rw, the
// QEMU CVE-2016-2841 class). The fixed code returns -1 and leaves CURR
// where it was; the vulnerable code copies out of bounds and advances
// CURR, so both assertions scream on a regression. Run under ASan to
// also catch the out-of-bounds write itself.

// PSTOP one page past chip memory lets CURR sit at the exclusive end,
// so the header write lands just past mem[] (first, non-wrapping copy).
TEST_F(Ne2000Test, RxFrameRefusesARingWithPstopBeyondChipMemory)
{
	SetUpValidRing();
	nic->s.page_start = kFirstPage;
	nic->s.page_stop  = kLastStopPage + 8;
	nic->s.bound_ptr  = kFirstPage;
	nic->s.curr_page  = kLastStopPage;

	EXPECT_EQ(nic->rx_frame(MakeFrame().data(), kMinFrameBytes), -1);
	EXPECT_EQ(nic->s.curr_page, kLastStopPage);
}

// CURR below the first valid page makes CURR*256 - MEMSTART negative,
// writing before mem[]. PSTOP stays valid, so CURR alone is the fault.
TEST_F(Ne2000Test, RxFrameRefusesARingWithCurrPageBelowChipMemory)
{
	SetUpValidRing();
	nic->s.page_start = kFirstPage;
	nic->s.page_stop  = kLastStopPage;
	nic->s.bound_ptr  = kFirstPage;
	nic->s.curr_page  = kFirstPage - 0x20;

	EXPECT_EQ(nic->rx_frame(MakeFrame().data(), kMinFrameBytes), -1);
	EXPECT_EQ(nic->s.curr_page, kFirstPage - 0x20);
}

// PSTOP and CURR at the register maximum overflow the next-page
// arithmetic: a checked build aborts in the 8-bit cast, an unchecked
// one truncates and the wrap-around copy length underflows. Guest
// crash either way; the fix refuses the ring first.
TEST_F(Ne2000Test, RxFrameRefusesARingAtTheRegisterMaximum)
{
	SetUpValidRing();
	nic->s.page_start = kFirstPage;
	nic->s.page_stop  = kPageAboveMemory;
	nic->s.bound_ptr  = kPageAboveMemory - 1;
	nic->s.curr_page  = kPageAboveMemory;

	EXPECT_EQ(nic->rx_frame(MakeFrame().data(), kMinFrameBytes), -1);
	EXPECT_EQ(nic->s.curr_page, kPageAboveMemory);
}

// The loopback transmit reads tx_bytes from the TPSR page onwards, so a
// valid page with an oversized count still walks off the end.
TEST_F(Ne2000Test, LoopbackTransmitAcceptsABufferInsideChipMemory)
{
	constexpr uint8_t kStartTxLoopback = 0x24;

	nic->s.TCR.loop_cntl  = 1;
	nic->s.tx_page_start  = kFirstPage;
	nic->s.tx_bytes       = kMinFrameBytes;

	nic->write_cr(kStartTxLoopback);

	EXPECT_EQ(nic->s.ISR.pkt_tx, 1);
}

TEST_F(Ne2000Test, LoopbackTransmitDropsABufferRunningPastChipMemory)
{
	constexpr uint8_t kStartTxLoopback = 0x24;

	nic->s.TCR.loop_cntl  = 1;
	nic->s.tx_page_start  = kLastPage;
	nic->s.tx_bytes       = 1024;

	nic->write_cr(kStartTxLoopback);

	EXPECT_EQ(nic->s.ISR.pkt_tx, 0);
}

} // namespace
