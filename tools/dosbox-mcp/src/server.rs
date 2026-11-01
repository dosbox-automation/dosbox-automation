use crate::client::DosBoxClient;
use crate::error::McpError;
use crate::types::*;
use rmcp::{
    Error as McpErrorInternal, handler::server::{
        router::tool::ToolRouter,
        tool::Parameters,
    },
    model::*,
    schemars, tool, tool_handler, tool_router,
    ServerHandler,
};
use serde::Deserialize;
use serde_json::json;
use std::future::Future;
use std::sync::Arc;

// Convert to error with context
fn to_mcp_error_ctx(endpoint: &str, err: McpError) -> McpErrorInternal {
    McpErrorInternal::internal_error(format!("{} - {}", endpoint, err), None)
}

/// MCP server for dosbox-automation
#[derive(Clone)]
pub struct DosBoxMcpServer {
    client: Arc<DosBoxClient>,
    tool_router: ToolRouter<Self>,
}

impl DosBoxMcpServer {
    pub fn new(client: Arc<DosBoxClient>) -> Self {
        Self {
            client,
            tool_router: Self::tool_router(),
        }
    }
}

#[tool_router]
impl DosBoxMcpServer {
    // ============ Status Tools ============

    #[tool(description = "Get current DOSBox status including program state and CPU info")]
    async fn get_status(&self) -> Result<CallToolResult, McpErrorInternal> {
        let status: Status = self.client.get("/status").await.map_err(|e| to_mcp_error_ctx("/status", e))?;
        let content = Content::json(status).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get the current program execution state")]
    async fn get_program_state(&self) -> Result<CallToolResult, McpErrorInternal> {
        let state: ProgramState = self.client.get("/program/state").await.map_err(|e| to_mcp_error_ctx("/program/state", e))?;
        let content = Content::json(state).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get DOSBox version and build information")]
    async fn get_dosbox_info(&self) -> Result<CallToolResult, McpErrorInternal> {
        let info: DosBoxInfo = self.client.get("/dosbox/info").await.map_err(|e| to_mcp_error_ctx("/dosbox/info", e))?;
        let content = Content::json(info).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Control Tools ============

    #[tool(description = "Shutdown DOSBox")]
    async fn shutdown(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/control/shutdown", &json!({})).await.map_err(|e| to_mcp_error_ctx("/control/shutdown", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Pause DOSBox emulation")]
    async fn pause(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/control/pause", &json!({})).await.map_err(|e| to_mcp_error_ctx("/control/pause", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Resume DOSBox emulation")]
    async fn resume(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/control/resume", &json!({})).await.map_err(|e| to_mcp_error_ctx("/control/resume", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get current pause state")]
    async fn get_pause_state(&self) -> Result<CallToolResult, McpErrorInternal> {
        // The API returns a boolean or simple object, use generic for now
        let state: serde_json::Value = self.client.get("/control/paused").await.map_err(|e| to_mcp_error_ctx("/control/paused", e))?;
        let content = Content::json(state).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ CPU Tools ============

    #[tool(description = "Get CPU register values")]
    async fn get_registers(&self) -> Result<CallToolResult, McpErrorInternal> {
        // API returns { registers: Registers }
        let response: serde_json::Value = self.client.get("/cpu/registers").await.map_err(|e| to_mcp_error_ctx("/cpu/registers", e))?;
        let regs = response.get("registers").cloned().unwrap_or(response);
        let content = Content::json(regs).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get current CPU cycles setting")]
    async fn get_cycles(&self) -> Result<CallToolResult, McpErrorInternal> {
        let cycles: CpuCycles = self.client.get("/cpu/cycles").await.map_err(|e| to_mcp_error_ctx("/cpu/cycles", e))?;
        let content = Content::json(cycles).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Set CPU cycles (e.g., '5000', 'auto', 'max')")]
    async fn set_cycles(&self, Parameters(params): Parameters<SetCyclesParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "cycles": params.cycles });
        let result: SetCyclesResponse = self.client.put("/cpu/cycles", &body).await.map_err(|e| to_mcp_error_ctx("/cpu/cycles", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Set a CPU register value")]
    async fn set_register(&self, Parameters(params): Parameters<SetRegisterParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "register": params.register, "value": params.value });
        let result: ControlResponse = self.client.put("/cpu/register", &body).await.map_err(|e| to_mcp_error_ctx("/cpu/register", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Memory Tools ============

    #[tool(description = "Read memory at offset (max 128 MiB per request). Returns base64-encoded data.")]
    async fn read_memory(&self, Parameters(params): Parameters<ReadMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        if params.length > 128 * 1024 * 1024 {
            return Err(McpErrorInternal::invalid_params("max 128 MiB per request", None));
        }
        let path = format!("/memory/{}/{}", params.offset, params.length);
        let data: MemoryReadResponse = self.client.get(&path).await.map_err(|e| to_mcp_error_ctx(&path, e))?;
        let content = Content::json(data).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Write memory at offset (max 128 MiB per request). Data is base64-encoded.")]
    async fn write_memory(&self, Parameters(params): Parameters<WriteMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        if params.data.len() > 128 * 1024 * 1024 {
            return Err(McpErrorInternal::invalid_params("max 128 MiB per request", None));
        }
        let path = format!("/memory/{}", params.offset);
        let body = json!({ "data": base64::Engine::encode(&base64::engine::general_purpose::STANDARD, &params.data) });
        let result: MemoryWriteResponse = self.client.put(&path, &body).await.map_err(|e| to_mcp_error_ctx(&path, e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Search memory for a value (byte/word/dword). Max 16 MiB per request.")]
    async fn search_memory(&self, Parameters(params): Parameters<SearchMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        let end = params.start.unwrap_or(0) + params.length.unwrap_or(16 * 1024 * 1024) as u64;
        let body = json!({
            "value": params.value,
            "width": params.width,
            "start": params.start.unwrap_or(0),
            "end": end,
        });
        let result: MemorySearchResponse = self.client.post("/memory/search", &body).await.map_err(|e| to_mcp_error_ctx("/memory/search", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Allocate memory in DOSBox")]
    async fn allocate_memory(&self, Parameters(params): Parameters<AllocateMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "size": params.size, "area": params.area, "strategy": params.strategy });
        let result: MemoryAllocateResponse = self.client.post("/memory/allocate", &body).await.map_err(|e| to_mcp_error_ctx("/memory/allocate", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Free previously allocated memory")]
    async fn free_memory(&self, Parameters(params): Parameters<FreeMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "addr": params.addr });
        let result: MemoryFreeResponse = self.client.post("/memory/free", &body).await.map_err(|e| to_mcp_error_ctx("/memory/free", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Freeze Memory Tools ============

    #[tool(description = "Set a trainer-style value freeze (pins memory to fixed value each frame)")]
    async fn freeze_memory(&self, Parameters(params): Parameters<FreezeMemoryParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "address": params.address, "value": params.value, "width": params.width });
        let result: FreezeResponse = self.client.post("/memory/freeze", &body).await.map_err(|e| to_mcp_error_ctx("/memory/freeze", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "List active value freezes")]
    async fn list_freezes(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ListFreezesResponse = self.client.get("/memory/freeze").await.map_err(|e| to_mcp_error_ctx("/memory/freeze", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Remove freezes. With no address, clears all freezes.")]
    async fn clear_freezes(&self, Parameters(params): Parameters<ClearFreezesParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = if let Some(addr) = params.address {
            json!({ "address": addr })
        } else {
            json!({})
        };
        let result: DeleteFreezesResponse = self.client.delete_with_body("/memory/freeze", &body).await.map_err(|e| to_mcp_error_ctx("/memory/freeze", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Input Tools ============

    #[tool(description = "Type text into DOSBox at specified characters per second (default: 30 cps)")]
    async fn type_text(&self, Parameters(params): Parameters<TypeTextParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "text": params.text, "cps": params.cps });
        let result: TypeTextResponse = self.client.post("/input/type", &body).await.map_err(|e| to_mcp_error_ctx("/input/type", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Send a sequence of input events (keys, mouse, wheel) with optional timing")]
    async fn send_input_sequence(&self, Parameters(params): Parameters<SendInputSequenceParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "events": params.events });
        let result: InputSequenceResponse = self.client.post("/input/sequence", &body).await.map_err(|e| to_mcp_error_ctx("/input/sequence", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Video/Frame Tools ============

    #[tool(description = "Capture a frame (raw or rendered) as PNG/JPEG/raw. Returns base64-encoded image.")]
    async fn capture_frame(&self, Parameters(params): Parameters<CaptureFrameParams>) -> Result<CallToolResult, McpErrorInternal> {
        let path = format!("/video/frame?mode={}&format={}&quality={}", params.mode, params.format, params.quality);
        let bytes = self.client.get_bytes(&path).await.map_err(|e| to_mcp_error_ctx(&path, e))?;
        
        let base64 = base64::Engine::encode(&base64::engine::general_purpose::STANDARD, &bytes);
        let result = CaptureFrameResult {
            data: base64,
            size: bytes.len(),
            format: params.format,
            mode: params.mode,
            width: None,
            height: None,
        };
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get frame dimensions and format info")]
    async fn get_frame_info(&self) -> Result<CallToolResult, McpErrorInternal> {
        let info: FrameInfo = self.client.get("/video/frame/info").await.map_err(|e| to_mcp_error_ctx("/video/frame/info", e))?;
        let content = Content::json(info).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Read text-mode screen content (useful for driving installers)")]
    async fn read_screen_text(&self) -> Result<CallToolResult, McpErrorInternal> {
        let text: ScreenText = self.client.get("/video/text").await.map_err(|e| to_mcp_error_ctx("/video/text", e))?;
        let content = Content::json(text).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Recording Tools ============

    #[tool(description = "Start recording input sequence")]
    async fn start_recording(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/input/record/start", &json!({})).await.map_err(|e| to_mcp_error_ctx("/input/record/start", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Pause/resume input recording")]
    async fn pause_recording(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/input/record/pause", &json!({})).await.map_err(|e| to_mcp_error_ctx("/input/record/pause", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Stop input recording and return recorded data")]
    async fn stop_recording(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: RecordingResult = self.client.post("/input/record/stop", &json!({})).await.map_err(|e| to_mcp_error_ctx("/input/record/stop", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get current recording status")]
    async fn get_recording_status(&self) -> Result<CallToolResult, McpErrorInternal> {
        let status: RecordingStatus = self.client.get("/input/record/status").await.map_err(|e| to_mcp_error_ctx("/input/record/status", e))?;
        let content = Content::json(status).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Drive Tools ============

    #[tool(description = "Swap floppy/CD image in a drive")]
    async fn swap_disk(&self, Parameters(params): Parameters<SwapDiskParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "drive": params.drive, "image": params.image });
        let result: DriveSwapResponse = self.client.post("/drive/swap", &body).await.map_err(|e| to_mcp_error_ctx("/drive/swap", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Lock or unlock drive mounts")]
    async fn lock_mounts(&self, Parameters(params): Parameters<LockMountsParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "locked": params.lock });
        let result: MountLockResponse = self.client.post("/mount/lock", &body).await.map_err(|e| to_mcp_error_ctx("/mount/lock", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get current mount lock state")]
    async fn get_mount_lock(&self) -> Result<CallToolResult, McpErrorInternal> {
        let state: MountLockStatus = self.client.get("/mount/lock").await.map_err(|e| to_mcp_error_ctx("/mount/lock", e))?;
        let content = Content::json(state).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Script Tools ============

    #[tool(description = "Load a Lua script")]
    async fn load_script(&self, Parameters(params): Parameters<LoadScriptParams>) -> Result<CallToolResult, McpErrorInternal> {
        // The API takes the script as text/plain body with query params
        let path = format!("/script/load{}", params.name.as_ref().map(|n| format!("?name={}", urlencoding::encode(n))).unwrap_or_default());
        let result: ScriptLoadResponse = self.client.post(&path, &params.script).await.map_err(|e| to_mcp_error_ctx(&path, e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Start executing loaded Lua script")]
    async fn start_script(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/script/start", &json!({})).await.map_err(|e| to_mcp_error_ctx("/script/start", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Stop running Lua script")]
    async fn stop_script(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/script/stop", &json!({})).await.map_err(|e| to_mcp_error_ctx("/script/stop", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get Lua script execution status")]
    async fn get_script_status(&self) -> Result<CallToolResult, McpErrorInternal> {
        let status: ScriptStatus = self.client.get("/script/status").await.map_err(|e| to_mcp_error_ctx("/script/status", e))?;
        let content = Content::json(status).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ Capture Tools ============

    #[tool(description = "Start video capture (ZMBV)")]
    async fn start_video_capture(&self, Parameters(params): Parameters<StartVideoCaptureParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({
            "mode": params.mode,
        });
        let result: CaptureStartResponse = self.client.post("/capture/video/start", &body).await.map_err(|e| to_mcp_error_ctx("/capture/video/start", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Stop video capture")]
    async fn stop_video_capture(&self) -> Result<CallToolResult, McpErrorInternal> {
        let result: ControlResponse = self.client.post("/capture/video/stop", &json!({})).await.map_err(|e| to_mcp_error_ctx("/capture/video/stop", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get video capture status")]
    async fn get_capture_status(&self) -> Result<CallToolResult, McpErrorInternal> {
        let status: CaptureStatus = self.client.get("/capture/video/status").await.map_err(|e| to_mcp_error_ctx("/capture/video/status", e))?;
        let content = Content::json(status).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Get video capture compression settings")]
    async fn get_compression(&self) -> Result<CallToolResult, McpErrorInternal> {
        let comp: CaptureCompression = self.client.get("/capture/video/compression").await.map_err(|e| to_mcp_error_ctx("/capture/video/compression", e))?;
        let content = Content::json(comp).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Set video capture compression settings")]
    async fn set_compression(&self, Parameters(params): Parameters<SetCompressionParams>) -> Result<CallToolResult, McpErrorInternal> {
        let mut body = serde_json::Map::new();
        if let Some(raw) = params.raw {
            body.insert("raw".to_string(), json!(raw));
        }
        if let Some(rendered) = params.rendered {
            body.insert("rendered".to_string(), json!(rendered));
        }
        let result: CaptureCompression = self.client.put("/capture/video/compression", &body).await.map_err(|e| to_mcp_error_ctx("/capture/video/compression", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ I/O Tools ============

    #[tool(description = "Read I/O port (byte or word)")]
    async fn read_port(&self, Parameters(params): Parameters<ReadPortParams>) -> Result<CallToolResult, McpErrorInternal> {
        let path = format!("/io/port?port={}&width={}", params.port, params.width);
        let result: PortReadResponse = self.client.get(&path).await.map_err(|e| to_mcp_error_ctx(&path, e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    #[tool(description = "Write I/O port (byte or word)")]
    async fn write_port(&self, Parameters(params): Parameters<WritePortParams>) -> Result<CallToolResult, McpErrorInternal> {
        let body = json!({ "port": params.port, "value": params.value, "width": params.width });
        let result: PortWriteResponse = self.client.put("/io/port", &body).await.map_err(|e| to_mcp_error_ctx("/io/port", e))?;
        let content = Content::json(result).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }

    // ============ DOS Tools ============

    #[tool(description = "Get DOS internals (MCB map, etc.)")]
    async fn get_dos_internals(&self) -> Result<CallToolResult, McpErrorInternal> {
        let internals: DosInternals = self.client.get("/dos/internals").await.map_err(|e| to_mcp_error_ctx("/dos/internals", e))?;
        let content = Content::json(internals).map_err(|e| McpErrorInternal::internal_error(e.to_string(), None))?;
        Ok(CallToolResult::success(vec![content]))
    }
}

// ============ Parameter Types ============

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SetCyclesParams {
    /// CPU cycles value (e.g., "5000", "auto", "max")
    pub cycles: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SetRegisterParams {
    /// Register name (e.g., "eax", "ebx", "eip")
    pub register: String,
    /// Value to set
    pub value: u64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct ReadMemoryParams {
    /// Physical memory offset
    pub offset: u64,
    /// Number of bytes to read (max 128 MiB)
    pub length: u32,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct WriteMemoryParams {
    /// Physical memory offset
    pub offset: u64,
    /// Base64-encoded data to write
    pub data: Vec<u8>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SearchMemoryParams {
    /// Value to search for
    pub value: u64,
    /// Width in bytes: 1 (byte), 2 (word), 4 (dword)
    #[serde(default = "default_width")]
    pub width: u8,
    /// Start address (default: 0)
    #[serde(default)]
    pub start: Option<u64>,
    /// Number of bytes to scan (max 16 MiB)
    #[serde(default)]
    pub length: Option<u32>,
}

fn default_width() -> u8 {
    1
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct AllocateMemoryParams {
    /// Size in bytes
    pub size: u32,
    /// Memory area: "CONV", "UMB", "XMS", "EMS"
    #[serde(default = "default_area")]
    pub area: String,
    /// Allocation strategy: "BEST_FIT", "FIRST_FIT", "LAST_FIT"
    #[serde(default = "default_strategy")]
    pub strategy: String,
}

fn default_area() -> String {
    "CONV".to_string()
}

fn default_strategy() -> String {
    "BEST_FIT".to_string()
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct FreeMemoryParams {
    /// Physical address of allocated block
    pub addr: u64,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct FreezeMemoryParams {
    /// Physical address to freeze
    pub address: u64,
    /// Value to freeze
    pub value: u64,
    /// Width in bytes: 1, 2, or 4
    #[serde(default = "default_width")]
    pub width: u8,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct ClearFreezesParams {
    /// Specific address to clear (omit to clear all)
    pub address: Option<u64>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct TypeTextParams {
    /// Text to type
    pub text: String,
    /// Characters per second (default: 30)
    #[serde(default = "default_cps")]
    pub cps: f32,
}

fn default_cps() -> f32 {
    30.0
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SendInputSequenceParams {
    /// Array of input events
    pub events: Vec<InputEvent>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct CaptureFrameParams {
    /// Frame source: "raw" (emulator framebuffer) or "rendered" (post-shader)
    #[serde(default = "default_mode")]
    pub mode: String,
    /// Output format: "png", "jpeg", "raw"
    #[serde(default = "default_format")]
    pub format: String,
    /// JPEG quality 1-100 (default: 85)
    #[serde(default = "default_quality")]
    pub quality: u8,
}

fn default_mode() -> String {
    "raw".to_string()
}

fn default_format() -> String {
    "png".to_string()
}

fn default_quality() -> u8 {
    85
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SwapDiskParams {
    /// Drive letter (e.g., "A", "C")
    pub drive: String,
    /// Path to disk image
    pub image: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct LockMountsParams {
    /// Lock state
    pub lock: bool,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct LoadScriptParams {
    /// Lua script source code
    pub script: String,
    /// Optional name for the script
    pub name: Option<String>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct StartVideoCaptureParams {
    /// Capture mode: "raw" or "rendered"
    #[serde(default = "default_mode")]
    pub mode: String,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct SetCompressionParams {
    /// Compression level for raw mode (0-9)
    pub raw: Option<u8>,
    /// Compression level for rendered mode (0-9)
    pub rendered: Option<u8>,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct ReadPortParams {
    /// Port number (0-65535)
    pub port: u16,
    /// Width: 1 for byte, 2 for word
    #[serde(default = "default_width")]
    pub width: u8,
}

#[derive(Debug, Deserialize, schemars::JsonSchema)]
pub struct WritePortParams {
    /// Port number (0-65535)
    pub port: u16,
    /// Value to write
    pub value: u32,
    /// Width: 1 for byte, 2 for word
    #[serde(default = "default_width")]
    pub width: u8,
}

// ============ ServerHandler Implementation ============

#[tool_handler]
impl ServerHandler for DosBoxMcpServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo {
            instructions: Some("MCP server for dosbox-automation REST API. Provides tools for controlling DOSBox instances including input injection, memory access, video capture, drive management, and Lua scripting.".into()),
            capabilities: ServerCapabilities::builder().enable_tools().build(),
            ..Default::default()
        }
    }
}