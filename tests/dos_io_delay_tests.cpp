// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include <gtest/gtest.h>

#include "cpu/cpu.h"

// Wrapper defined in dos.cpp, exposing the static modify_cycles.
extern void dos_test_modify_cycles(Bits value);

namespace {

// The fix (cherry-picked from dosbox-staging PR #4991) ensures that
// DOS file I/O delay is applied unconditionally, regardless of the
// remaining CPU slice. Before the fix, modify_cycles capped the delay
// at CPU_Cycles, making I/O speed dependent on PIC event density.

class DosIoDelay : public ::testing::Test {
protected:
	void SetUp() override
	{
		CPU_Cycles = 0;
		CPU_IODelayRemoved = 0;
	}
};

TEST_F(DosIoDelay, full_delay_applied_with_large_slice)
{
	CPU_Cycles = 100000;
	dos_test_modify_cycles(1000);

	EXPECT_EQ(CPU_Cycles, 100000 - 4000);
	EXPECT_EQ(CPU_IODelayRemoved, 4000);
}

TEST_F(DosIoDelay, full_delay_applied_with_small_slice)
{
	// This is the bug scenario: small CPU slice (per-scanline VGA
	// rendering gives ~600 cycles per slice at 20k cycles). The old
	// code would cap the delay at CPU_Cycles and set it to 5.
	CPU_Cycles = 600;
	dos_test_modify_cycles(1000);

	// Must go negative - the tick accounting absorbs the remainder.
	EXPECT_EQ(CPU_Cycles, 600 - 4000);
	EXPECT_EQ(CPU_IODelayRemoved, 4000);
}

TEST_F(DosIoDelay, delay_independent_of_slice_size)
{
	// The core invariant: identical I/O operations must produce
	// identical delays regardless of the CPU slice they run in.
	// This is what the old code violated.
	constexpr Bits transfer_size = 2048;
	constexpr int64_t expected_delay = 4 * transfer_size;

	// Large slice (chunked VGA rendering, ~20k per slice)
	CPU_Cycles = 20000;
	CPU_IODelayRemoved = 0;
	dos_test_modify_cycles(transfer_size);
	const auto delay_large_slice = CPU_IODelayRemoved;

	// Small slice (per-scanline VGA rendering, ~600 per slice)
	CPU_Cycles = 600;
	CPU_IODelayRemoved = 0;
	dos_test_modify_cycles(transfer_size);
	const auto delay_small_slice = CPU_IODelayRemoved;

	// Tiny slice (very dense PIC events)
	CPU_Cycles = 50;
	CPU_IODelayRemoved = 0;
	dos_test_modify_cycles(transfer_size);
	const auto delay_tiny_slice = CPU_IODelayRemoved;

	EXPECT_EQ(delay_large_slice, expected_delay);
	EXPECT_EQ(delay_small_slice, expected_delay);
	EXPECT_EQ(delay_tiny_slice, expected_delay);
}

TEST_F(DosIoDelay, cpu_cycles_goes_negative)
{
	CPU_Cycles = 100;
	dos_test_modify_cycles(500);

	EXPECT_LT(CPU_Cycles, 0);
	EXPECT_EQ(CPU_Cycles, 100 - 2000);
}

TEST_F(DosIoDelay, zero_transfer_no_delay)
{
	CPU_Cycles = 10000;
	dos_test_modify_cycles(0);

	EXPECT_EQ(CPU_Cycles, 10000);
	EXPECT_EQ(CPU_IODelayRemoved, 0);
}

TEST_F(DosIoDelay, single_byte_transfer)
{
	CPU_Cycles = 10000;
	dos_test_modify_cycles(1);

	EXPECT_EQ(CPU_Cycles, 10000 - 4);
	EXPECT_EQ(CPU_IODelayRemoved, 4);
}

TEST_F(DosIoDelay, accumulated_delay_across_multiple_reads)
{
	// Simulates a game startup doing many file reads (the Ishar 3
	// scenario: ~280 KB across 10 reads).
	CPU_Cycles = 20000;

	dos_test_modify_cycles(1024);
	dos_test_modify_cycles(2048);
	dos_test_modify_cycles(4096);

	const int64_t expected = 4 * (1024 + 2048 + 4096);
	EXPECT_EQ(CPU_IODelayRemoved, expected);
}

} // namespace
