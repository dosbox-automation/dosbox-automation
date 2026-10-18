// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
// The GUS seen from a DOS program: its I/O ports, the 8237 and the PIC.
// Register meanings per the UltraSound Lowlevel Toolkit, revision 2.01.

#include "hardware/audio/gus.h"

#include "config/setup.h"
#include "dosbox_test_fixture.h"
#include "hardware/dma.h"
#include "hardware/memory.h"
#include "hardware/pic.h"
#include "hardware/port.h"
#include "hardware/timer.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

constexpr io_port_t MixControlPort    = 0x240;
constexpr io_port_t IrqStatusPort     = 0x246;
constexpr io_port_t AddressSelectPort = 0x24b;
constexpr io_port_t RegisterSelect    = 0x343;
constexpr io_port_t DataLowPort       = 0x344;
constexpr io_port_t DataHighPort      = 0x345;
constexpr io_port_t DramIoPort        = 0x347;

constexpr uint8_t DramDmaControl    = 0x41;
constexpr uint8_t DmaStartAddress   = 0x42;
constexpr uint8_t DramAddressLow    = 0x43;
constexpr uint8_t DramAddressHigh   = 0x44;
constexpr uint8_t SamplingFrequency = 0x48;
constexpr uint8_t SamplingControl   = 0x49;
constexpr uint8_t ResetRegister     = 0x4c;

constexpr uint8_t CardRunning = 0b111; // run, DAC on, IRQs on

constexpr uint8_t SampleEnable    = 0x01;
constexpr uint8_t SampleStereo    = 0x02;
constexpr uint8_t SampleIrq       = 0x20;
constexpr uint8_t SampleInvertMsb = 0x80;
constexpr uint8_t PendingIrqBit   = 0x40;
constexpr uint8_t DmaTcIrqBit     = 0x80;

// 9878400 / (16 * (26 + 2)) = 22050 Hz, section 2.6.1.6
constexpr uint8_t Rate22050Hz = 26;

constexpr uint8_t GusIrq = 5;

constexpr uint8_t DmaModeSingle     = 0x40;
constexpr uint8_t DmaModeToMemory   = 0x04;
constexpr uint8_t DmaModeFromMemory = 0x08;
constexpr io_port_t DmaMaskPort     = 0x0a;
constexpr io_port_t DmaModePort     = 0x0b;
constexpr io_port_t DmaFlipFlopPort = 0x0c;
constexpr uint8_t DmaPagePorts[]    = {0x87, 0x83, 0x81, 0x82};

constexpr PhysPt RecordTarget = 0x50000;
constexpr PhysPt OtherTarget  = 0x60000;
constexpr uint8_t Untouched   = 0x55;

void write_register8(const uint8_t reg, const uint8_t value)
{
	IO_WriteB(RegisterSelect, reg);
	IO_WriteB(DataHighPort, value);
}

void write_register16(const uint8_t reg, const uint16_t value)
{
	IO_WriteB(RegisterSelect, reg);
	IO_WriteW(DataLowPort, value);
}

uint8_t read_register8(const uint8_t reg)
{
	IO_WriteB(RegisterSelect, reg);
	return static_cast<uint8_t>(IO_ReadB(DataHighPort));
}

uint8_t read_gus_ram(const uint32_t address)
{
	write_register16(DramAddressLow, static_cast<uint16_t>(address & 0xffff));
	write_register8(DramAddressHigh, static_cast<uint8_t>((address >> 16) & 0xf));
	return static_cast<uint8_t>(IO_ReadB(DramIoPort));
}

// Programs an 8-bit channel (0 to 3) of the first 8237 and leaves it masked.
void program_dma8(const uint8_t channel, const uint8_t direction,
                  const PhysPt address, const uint16_t bytes)
{
	const auto addr_port  = static_cast<io_port_t>(channel * 2);
	const auto count_port = static_cast<io_port_t>(channel * 2 + 1);
	const auto count      = static_cast<uint16_t>(bytes - 1);

	IO_WriteB(DmaMaskPort, 0x04 | channel);
	IO_WriteB(DmaModePort, DmaModeSingle | direction | channel);
	IO_WriteB(DmaFlipFlopPort, 0);
	IO_WriteB(addr_port, address & 0xff);
	IO_WriteB(addr_port, (address >> 8) & 0xff);
	IO_WriteB(DmaPagePorts[channel], (address >> 16) & 0xff);
	IO_WriteB(DmaFlipFlopPort, 0);
	IO_WriteB(count_port, count & 0xff);
	IO_WriteB(count_port, (count >> 8) & 0xff);
}

// Programs channel 5 of the second 8237 (16-bit, word addressed) and leaves
// it masked.
void program_dma5(const uint8_t direction, const PhysPt address, const uint16_t bytes)
{
	constexpr uint8_t Index = 1; // channel 5 on the second controller
	const auto words        = static_cast<uint16_t>(bytes / 2 - 1);
	const auto word_address = static_cast<uint16_t>((address & 0x1ffff) >> 1);

	IO_WriteB(0xd4, 0x04 | Index);
	IO_WriteB(0xd6, DmaModeSingle | direction | Index);
	IO_WriteB(0xd8, 0);
	IO_WriteB(0xc4, word_address & 0xff);
	IO_WriteB(0xc4, (word_address >> 8) & 0xff);
	IO_WriteB(0x8b, (address >> 16) & 0xff);
	IO_WriteB(0xd8, 0);
	IO_WriteB(0xc6, words & 0xff);
	IO_WriteB(0xc6, (words >> 8) & 0xff);
}

void unmask_dma5()
{
	IO_WriteB(0xd4, 1);
}

void unmask_dma(const uint8_t channel)
{
	IO_WriteB(DmaMaskPort, channel);
}

void fill_memory(const PhysPt address, const int bytes, const uint8_t value)
{
	for (int i = 0; i < bytes; ++i) {
		mem_writeb(address + static_cast<PhysPt>(i), value);
	}
}

uint8_t pic_master_irr()
{
	IO_WriteB(0x20, 0x0a);
	return static_cast<uint8_t>(IO_ReadB(0x20));
}

void run_ms(const int ms)
{
	for (int i = 0; i < ms; ++i) {
		TIMER_AddTick();
		PIC_RunQueue();
	}
}

class GusDmaTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		set_section_property_value("gus", "gus", "on");
		set_section_property_value("gus", "gusbase", "240");
		set_section_property_value("gus", "gusirq", "5");
		set_section_property_value("gus", "gusdma", "3");
		GUS_Init();
		write_register8(ResetRegister, CardRunning);
	}

	void TearDown() override
	{
		GUS_Destroy();
		// The GUS made the DMA controller; it must go while the logger
		// lives.
		DMA_Destroy();
		DOSBoxTestFixture::TearDown();
	}

	// Records `bytes` into RecordTarget over channel 3 with the given
	// Sampling Control bits and returns once the count has run out.
	void record(const uint8_t sampling_control, const uint16_t bytes)
	{
		fill_memory(RecordTarget, bytes + 16, Untouched);
		program_dma8(3, DmaModeToMemory, RecordTarget, bytes);
		write_register8(SamplingFrequency, Rate22050Hz);
		unmask_dma(3);
		write_register8(SamplingControl, sampling_control);
		run_ms(50);
	}
};

TEST_F(GusDmaTest, SamplingWithoutDramSetupDoesNotThrow)
{
	write_register8(SamplingControl, SampleEnable);

	EXPECT_NO_THROW(run_ms(5));
}

TEST_F(GusDmaTest, RecordingAfterAnUploadOnTheSharedChannelFillsTheBuffer)
{
	program_dma8(3, DmaModeFromMemory, OtherTarget, 64);
	write_register16(DmaStartAddress, 0);
	unmask_dma(3);
	write_register8(DramDmaControl, SampleEnable);
	run_ms(5);
	ASSERT_TRUE(DMA_GetChannel(3)->has_reached_terminal_count);
	write_register8(DramDmaControl, 0);

	// With the upload switched off, its terminal count still sits in the
	// 8237 status until port 08h is read; the recording must not stop on
	// it.
	record(SampleEnable, 64);

	for (PhysPt i = 0; i < 64; ++i) {
		ASSERT_EQ(mem_readb(RecordTarget + i), 0x80) << "byte " << i;
	}
}

TEST_F(GusDmaTest, SixteenBitDataOnAnEightBitChannelKeepsTheSamplePhase)
{
	constexpr uint8_t SixteenBitData = 0x40;
	constexpr uint16_t Bytes         = 1000;
	record(SampleEnable | SixteenBitData, Bytes);

	for (PhysPt i = 0; i < Bytes; i += 2) {
		ASSERT_EQ(mem_readb(RecordTarget + i), 0x00) << "byte " << i;
		ASSERT_EQ(mem_readb(RecordTarget + i + 1), 0x80) << "byte " << i + 1;
	}
}

TEST_F(GusDmaTest, ARecordChannelSharedWithAnotherDeviceKeepsItsCallback)
{
	int other_device_events = 0;
	auto* channel1          = DMA_GetChannel(1);
	channel1->RegisterCallback(
	        [&](const DmaChannel*, DmaEvent) { ++other_device_events; });

	IO_WriteB(MixControlPort, 0x0b);
	IO_WriteB(AddressSelectPort, (1 << 3) | 2);
	GUS_Destroy();

	ASSERT_TRUE(channel1->callback);
	const auto events_before = other_device_events;
	unmask_dma(1);
	EXPECT_EQ(other_device_events, events_before + 1);

	channel1->RegisterCallback({});
}

TEST_F(GusDmaTest, ARecordChannelOnAClosedSecondaryControllerIsNotKept)
{
	// 2XB: DMA select 1 = DMA3, DMA select 2 = DMA5. Without an assertion of
	// its own: a kept channel pointer shows as a use after free under ASan.
	IO_WriteB(MixControlPort, 0x0b);
	IO_WriteB(AddressSelectPort, (3 << 3) | 2);
	DMA_ShutdownSecondaryController();

	write_register8(SamplingControl, SampleEnable);
	run_ms(5);
	GUS_Destroy();
}

TEST_F(GusDmaTest, UnmaskingBeforeAnyDramSetupDoesNotThrow)
{
	program_dma8(3, DmaModeFromMemory, OtherTarget, 64);
	unmask_dma(3);

	EXPECT_NO_THROW(run_ms(5));
}

TEST_F(GusDmaTest, RecordingWritesUnsignedSilence)
{
	record(SampleEnable, 64);

	for (PhysPt i = 0; i < 64; ++i) {
		ASSERT_EQ(mem_readb(RecordTarget + i), 0x80) << "byte " << i;
	}
	EXPECT_EQ(mem_readb(RecordTarget + 64), Untouched);
	EXPECT_TRUE(DMA_GetChannel(3)->has_reached_terminal_count);
}

TEST_F(GusDmaTest, RecordingWithInvertedMsbWritesZeroSilence)
{
	record(SampleEnable | SampleInvertMsb, 64);

	for (PhysPt i = 0; i < 64; ++i) {
		ASSERT_EQ(mem_readb(RecordTarget + i), 0x00) << "byte " << i;
	}
	EXPECT_EQ(mem_readb(RecordTarget + 64), Untouched);
}

TEST_F(GusDmaTest, RecordingEndRaisesTheDmaIrqWhenAsked)
{
	record(SampleEnable | SampleIrq, 64);

	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, DmaTcIrqBit);
	EXPECT_EQ(pic_master_irr() & (1 << GusIrq), 1 << GusIrq);
}

TEST_F(GusDmaTest, ReadingSamplingControlAcknowledgesTheRecordIrq)
{
	record(SampleEnable | SampleIrq, 64);

	EXPECT_EQ(read_register8(SamplingControl) & PendingIrqBit, PendingIrqBit);
	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, 0);
	EXPECT_EQ(read_register8(SamplingControl) & PendingIrqBit, 0);
}

TEST_F(GusDmaTest, AcknowledgingTheDramIrqKeepsAPendingRecordIrq)
{
	record(SampleEnable | SampleIrq, 64);

	read_register8(DramDmaControl);

	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, DmaTcIrqBit);
	EXPECT_EQ(pic_master_irr() & (1 << GusIrq), 1 << GusIrq);
	read_register8(SamplingControl);
	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, 0);
}

TEST_F(GusDmaTest, RecordingOverASixteenBitChannelFillsTheBuffer)
{
	constexpr uint8_t SixteenBitData = 0x40;
	constexpr uint16_t Bytes         = 128;
	IO_WriteB(MixControlPort, 0x0b);
	IO_WriteB(AddressSelectPort, (3 << 3) | 2); // select 2 = DMA5

	for (const uint8_t data_size : {uint8_t{0}, SixteenBitData}) {
		fill_memory(RecordTarget, Bytes + 16, Untouched);
		program_dma5(DmaModeToMemory, RecordTarget, Bytes);
		write_register8(SamplingFrequency, Rate22050Hz);
		unmask_dma5();
		write_register8(SamplingControl, SampleEnable | data_size);
		run_ms(50);

		for (PhysPt i = 0; i < Bytes; ++i) {
			const uint8_t expected = (data_size && i % 2 == 0) ? 0x00
			                                                   : 0x80;
			ASSERT_EQ(mem_readb(RecordTarget + i), expected)
			        << "byte " << i << ", data size "
			        << int(data_size);
		}
		EXPECT_EQ(mem_readb(RecordTarget + Bytes), Untouched);
	}
}

// UltraSound Lowlevel Toolkit 2.01, 3.5.32 and 3.5.5: an autoinit recording is
// restarted by writing the control register again, without reprogramming the
// 8237.
TEST_F(GusDmaTest, AWrite49hRestartsAnAutoinitRecordingFromItsStart)
{
	constexpr uint8_t DmaModeAutoinit = 0x10;
	fill_memory(RecordTarget, 80, Untouched);
	program_dma8(3, DmaModeToMemory | DmaModeAutoinit, RecordTarget, 64);
	write_register8(SamplingFrequency, Rate22050Hz);
	unmask_dma(3);
	write_register8(SamplingControl, SampleEnable);
	run_ms(50);
	ASSERT_EQ(mem_readb(RecordTarget + 63), 0x80);

	fill_memory(RecordTarget, 80, Untouched);
	run_ms(5);
	ASSERT_EQ(mem_readb(RecordTarget), Untouched) << "kept recording past TC";

	write_register8(SamplingControl, SampleEnable);
	run_ms(50);

	EXPECT_EQ(mem_readb(RecordTarget), 0x80);
	EXPECT_EQ(mem_readb(RecordTarget + 63), 0x80);
	EXPECT_EQ(mem_readb(RecordTarget + 64), Untouched);
}

TEST_F(GusDmaTest, EnablingTheIrqWithAPendingRecordEndRaisesIt)
{
	record(SampleEnable, 64);
	ASSERT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, 0);

	write_register8(SamplingControl, SampleIrq);

	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, DmaTcIrqBit);
}

TEST_F(GusDmaTest, ClearingTheDramIrqEnableDropsItsPendingIrq)
{
	program_dma8(3, DmaModeFromMemory, OtherTarget, 64);
	unmask_dma(3);
	write_register8(DramDmaControl, SampleEnable | SampleIrq);
	run_ms(5);
	ASSERT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, DmaTcIrqBit);

	write_register8(DramDmaControl, 0);

	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, 0);
}

TEST_F(GusDmaTest, RecordingEndWithoutIrqEnableRaisesNoIrq)
{
	record(SampleEnable, 64);

	EXPECT_EQ(IO_ReadB(IrqStatusPort) & DmaTcIrqBit, 0);
	EXPECT_EQ(pic_master_irr() & (1 << GusIrq), 0);
	EXPECT_EQ(read_register8(SamplingControl) & PendingIrqBit, PendingIrqBit);
}

TEST_F(GusDmaTest, StereoRecordsTwiceTheBytesOfMono)
{
	constexpr uint16_t Plenty = 0x8000;

	auto bytes_after_100ms = [&](const uint8_t control) {
		program_dma8(3, DmaModeToMemory, RecordTarget, Plenty);
		write_register8(SamplingFrequency, Rate22050Hz);
		unmask_dma(3);
		write_register8(SamplingControl, control);
		run_ms(100);
		write_register8(SamplingControl, 0);
		return Plenty - (DMA_GetChannel(3)->curr_count + 1);
	};

	const auto mono   = bytes_after_100ms(SampleEnable);
	const auto stereo = bytes_after_100ms(SampleEnable | SampleStereo);

	// One event (1 ms) of phase either way.
	EXPECT_NEAR(mono, 2205, 23);
	EXPECT_NEAR(stereo, 2 * 2205, 45);
}

TEST_F(GusDmaTest, ReadingSamplingControlReturnsItsOwnBits)
{
	write_register8(DramDmaControl, SampleIrq);
	write_register8(SamplingControl, SampleStereo);

	EXPECT_EQ(read_register8(SamplingControl) & 0x3f, SampleStereo);
}

TEST_F(GusDmaTest, A16BitDataWriteDoesNotReadBackAsAPendingIrq)
{
	constexpr uint8_t SixteenBitData = 0x40;
	write_register8(DramDmaControl, SixteenBitData);
	write_register8(SamplingControl, SixteenBitData);

	EXPECT_EQ(read_register8(DramDmaControl) & PendingIrqBit, 0);
	EXPECT_EQ(read_register8(SamplingControl) & PendingIrqBit, 0);
}

TEST_F(GusDmaTest, DramUploadOnASharedChannelStillLandsInGusRam)
{
	for (PhysPt i = 0; i < 64; ++i) {
		mem_writeb(OtherTarget + i, static_cast<uint8_t>(i + 1));
	}
	program_dma8(3, DmaModeFromMemory, OtherTarget, 64);
	write_register16(DmaStartAddress, 0);
	unmask_dma(3);
	write_register8(DramDmaControl, SampleEnable);
	run_ms(5);

	for (uint32_t i = 0; i < 64; ++i) {
		ASSERT_EQ(read_gus_ram(i), i + 1) << "GUS RAM byte " << i;
	}
}

TEST_F(GusDmaTest, ASeparateRecordChannelGetsTheRecording)
{
	// 2XB with 2X0 bit 6 clear: DMA select 1 = DMA3, DMA select 2 = DMA1
	IO_WriteB(MixControlPort, 0x0b);
	IO_WriteB(AddressSelectPort, (1 << 3) | 2);

	fill_memory(RecordTarget, 80, Untouched);
	fill_memory(OtherTarget, 80, Untouched);
	program_dma8(1, DmaModeToMemory, RecordTarget, 64);
	program_dma8(3, DmaModeToMemory, OtherTarget, 64);
	write_register8(SamplingFrequency, Rate22050Hz);
	unmask_dma(1);
	unmask_dma(3);
	write_register8(SamplingControl, SampleEnable);
	run_ms(50);

	EXPECT_EQ(mem_readb(RecordTarget), 0x80);
	EXPECT_EQ(mem_readb(RecordTarget + 63), 0x80);
	EXPECT_EQ(mem_readb(OtherTarget), Untouched);
}

TEST_F(GusDmaTest, UnmaskingASeparateRecordChannelStartsAWaitingRecording)
{
	IO_WriteB(MixControlPort, 0x0b);
	IO_WriteB(AddressSelectPort, (1 << 3) | 2);

	fill_memory(RecordTarget, 80, Untouched);
	program_dma8(1, DmaModeToMemory, RecordTarget, 64);
	write_register8(SamplingFrequency, Rate22050Hz);
	write_register8(SamplingControl, SampleEnable);
	run_ms(5);
	ASSERT_EQ(mem_readb(RecordTarget), Untouched);

	unmask_dma(1);
	run_ms(50);

	EXPECT_EQ(mem_readb(RecordTarget + 63), 0x80);
}

TEST_F(GusDmaTest, ResettingTheCardStopsARecording)
{
	constexpr uint16_t Plenty = 0x8000;
	program_dma8(3, DmaModeToMemory, RecordTarget, Plenty);
	write_register8(SamplingFrequency, Rate22050Hz);
	unmask_dma(3);
	write_register8(SamplingControl, SampleEnable);
	run_ms(5);

	write_register8(ResetRegister, 0);
	const auto count_at_reset = DMA_GetChannel(3)->curr_count;
	run_ms(20);

	EXPECT_LT(count_at_reset, Plenty - 1);
	EXPECT_EQ(DMA_GetChannel(3)->curr_count, count_at_reset);
}

TEST_F(GusDmaTest, DestroyingTheCardDuringARecordingLeavesNoEventBehind)
{
	program_dma8(3, DmaModeToMemory, RecordTarget, 0x8000);
	write_register8(SamplingFrequency, Rate22050Hz);
	unmask_dma(3);
	write_register8(SamplingControl, SampleEnable);
	run_ms(2);

	GUS_Destroy();
	run_ms(5);
}

TEST_F(GusDmaTest, DestroyingTheCardDuringADramUploadLeavesNoEventBehind)
{
	program_dma8(3, DmaModeFromMemory, OtherTarget, 0x8000);
	unmask_dma(3);
	write_register8(DramDmaControl, SampleEnable);

	GUS_Destroy();
	run_ms(5);
}

} // namespace
