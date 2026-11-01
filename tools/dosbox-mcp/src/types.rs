//! Response types for dosbox-automation API
//! Generated from OpenAPI spec with schemars for MCP tool schemas

use serde::{Deserialize, Serialize};
use schemars::JsonSchema;

/// Overall emulator status
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Status {
    pub running: bool,
    pub shutdown_requested: bool,
    pub is_booted: bool,
    pub is_shell: bool,
    pub program: String,
    pub canonical_name: String,
}

/// Currently running program information
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ProgramState {
    pub segment_name: String,
    pub canonical_name: String,
    pub is_shell: bool,
    pub is_booted: bool,
}

/// DOSBox version and feature flags
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct DosBoxInfo {
    pub version: String,
    pub features: Features,
    pub binary: String,
    pub appimage: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Features {
    pub memory: bool,
    pub input: bool,
    pub cpu_registers: bool,
    pub cpu_control: bool,
    pub port_io: bool,
    pub freeze: bool,
    pub debugger: bool,
}

/// CPU register file
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct Registers {
    pub eax: u32,
    pub ebx: u32,
    pub ecx: u32,
    pub edx: u32,
    pub esi: u32,
    pub edi: u32,
    pub ebp: u32,
    pub esp: u32,
    pub eip: u32,
    pub eflags: u32,
    pub cs: u16,
    pub ds: u16,
    pub es: u16,
    pub fs: u16,
    pub gs: u16,
    pub ss: u16,
}

/// CPU cycles configuration
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct CpuCycles {
    pub mode: String, // "modern" | "legacy"
    pub real_mode: Option<u32>,
    pub protected_mode: Option<u32>,
    pub protected_mode_auto: bool,
    pub throttle: bool,
    pub current_max: u32,
    pub auto_adjust: bool,
    pub pmode: bool,
}

/// CPU cycles set response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct SetCyclesResponse {
    pub status: String,
    pub cycles: u32,
}

/// Memory read response (base64 encoded)
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MemoryReadResponse {
    pub data: String, // base64 encoded
}

/// Memory write response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MemoryWriteResponse {
    pub status: String,
}

/// Memory search response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MemorySearchResponse {
    pub matches: Vec<u64>,
}

/// Memory allocate response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MemoryAllocateResponse {
    pub addr: u64,
}

/// Memory free response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MemoryFreeResponse {
    pub status: String,
}

/// Freeze entry
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct FreezeEntry {
    pub address: u64,
    pub value: u64,
    pub width: u8, // 1, 2, or 4
}

/// Freeze response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct FreezeResponse {
    pub status: String,
    pub address: u64,
    pub value: u64,
    pub width: u8,
}

/// List freezes response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ListFreezesResponse {
    pub freezes: Vec<FreezeEntry>,
    pub count: usize,
}

/// Delete freezes response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct DeleteFreezesResponse {
    pub status: String,
    pub address: Option<u64>,
}

/// Input event types
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum InputEvent {
    Key {
        t: Option<f64>,
        key: String,
        pressed: bool,
    },
    MouseMove {
        t: Option<f64>,
        x_rel: f32,
        y_rel: f32,
        x_abs: Option<f32>,
        y_abs: Option<f32>,
    },
    MouseButton {
        t: Option<f64>,
        button: String, // "left", "right", "middle"
        pressed: bool,
    },
    MouseWheel {
        t: Option<f64>,
        delta: f32,
    },
}

/// Input sequence request
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct InputSequenceRequest {
    pub events: Vec<InputEvent>,
}

/// Input sequence response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct InputSequenceResponse {
    pub status: String,
    pub events_scheduled: usize,
}

/// Type text response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct TypeTextResponse {
    pub status: String,
    pub chars: usize,
}

/// Recording status
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct RecordingStatus {
    pub recording: bool,
    pub paused: bool,
    pub event_count: usize,
    pub duration_ms: f64,
}

/// Recording result (when stopping)
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct RecordingResult {
    pub event_count: usize,
    pub duration_ms: f64,
    pub events: Vec<InputEvent>,
}

/// Frame info metadata
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct FrameInfo {
    pub width: u32,
    pub height: u32,
    pub pixel_format: String,
    pub pitch: i32,
    pub is_paletted: bool,
}

/// Screen text response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ScreenText {
    pub is_text_mode: bool,
    pub bios_mode: u8,
    pub columns: u16,
    pub rows: u16,
    pub page: u8,
    pub text: String,
}

/// Drive swap response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct DriveSwapResponse {
    pub status: String,
    pub drive: String,
}

/// Mount lock response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MountLockResponse {
    pub status: String,
}

/// Mount lock status
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct MountLockStatus {
    pub locked: bool,
}

/// Script load response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ScriptLoadResponse {
    pub status: String,
    pub name: String,
}

/// Script status
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ScriptStatus {
    pub state: String, // "idle", "running", "stopped"
    pub frame: Option<u64>,
    pub name: Option<String>,
    pub output: Option<serde_json::Value>,
}

/// Capture start response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct CaptureStartResponse {
    pub status: String,
    pub mode: String, // "raw" | "rendered"
}

/// Capture status
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct CaptureStatus {
    pub capturing: bool,
    pub last_stop_reason: String, // "none", "clean", "write_error", "disk_space_low"
    pub mode: String,
}

/// Capture compression levels
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct CaptureCompression {
    pub raw: u8,
    pub rendered: u8,
}

/// I/O port read response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct PortReadResponse {
    pub port: u16,
    pub width: u8,
    pub value: u32,
}

/// I/O port write response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct PortWriteResponse {
    pub status: String,
}

/// DOS internals
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct DosInternals {
    pub list_of_lists: u64,
    pub dos_swappable_area: u64,
    pub first_shell: u64,
    pub memory_map: Vec<McbBlock>,
}

#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct McbBlock {
    pub segment: u32,
    pub type_: String,
    pub psp_segment: u32,
    pub size_paras: u32,
    pub size_bytes: u32,
    pub filename: String,
    pub is_last: bool,
}

/// Control response
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct ControlResponse {
    pub status: String,
}

/// Capture frame result
#[derive(Debug, Clone, Serialize, Deserialize, JsonSchema)]
pub struct CaptureFrameResult {
    pub data: String, // base64
    pub size: usize,
    pub format: String,
    pub mode: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
}