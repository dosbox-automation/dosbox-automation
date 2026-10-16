// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dosbox_test_fixture.h"

#include "hardware/audio/gus.h"
#include "hardware/pic.h"
#include "hardware/port.h"
#include "hardware/timer.h"

namespace {

constexpr io_port_t GusBasePort        = 0x240;
constexpr io_port_t RegisterSelectPort = GusBasePort + 0x103;
constexpr io_port_t DataHighBytePort   = GusBasePort + 0x105;

constexpr uint8_t SampleControlRegister = 0x49;
constexpr uint8_t ResetRegister         = 0x4c;

constexpr uint8_t RunCard                = 0b001;
constexpr uint8_t SampleControlEnableDma = 0x01;

// The GUS DMA event is scheduled less than 1 ms ahead; a few ticks is ample.
constexpr int TicksToRun = 3;

void write_register_high_byte(const uint8_t reg, const uint8_t value)
{
	IO_WriteB(RegisterSelectPort, reg);
	IO_WriteB(DataHighBytePort, value);
}

// Advance emulated time and run any PIC events that have become due.
void run_pic_events()
{
	for (int i = 0; i < TicksToRun; ++i) {
		TIMER_AddTick();
		PIC_RunQueue();
	}
}

// We need to initialize the PIC to add and test events
class HardwareAudioGusTest : public DOSBoxTestFixture {
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		PIC_Init();
	}

	void TearDown() override
	{
		PIC_Destroy();
		DOSBoxTestFixture::TearDown();
	}
};

// Regression: CNCD's GUSMAX CODEC setup (SDIWSS.DSD) enables a DMA event via
// register 0x49 without ever writing DMA-control register 0x41, leaving
// PerformDmaTransfer empty. The DMA event then threw std::bad_function_call.
TEST_F(HardwareAudioGusTest, dma_event_without_transfer_callback_does_not_throw)
{
	set_section_property_value("gus", "gus", "on");
	set_section_property_value("gus", "gusbase", "240");

	GUS_Init();

	// Start the GUS
	write_register_high_byte(ResetRegister, RunCard);

	// Queue the DMA event through register 0x49 only; never touch 0x41.
	write_register_high_byte(SampleControlRegister, SampleControlEnableDma);

	EXPECT_NO_THROW(run_pic_events());

	GUS_Destroy();
}

} // namespace