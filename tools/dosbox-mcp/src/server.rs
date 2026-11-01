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
    model::{Resource, ResourceTemplate, Prompt, PromptArgument, PromptMessage, PromptMessageRole, ResourceContents, PaginatedRequestParam, ListResourcesResult, ListResourceTemplatesResult, ReadResourceRequestParam, ReadResourceResult, GetPromptRequestParam, GetPromptResult, ListPromptsResult, RawResource, RawResourceTemplate},
    service::{RoleServer, RequestContext},
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

// ----- Documentation Resources -----
const API_REFERENCE_MD: &str = include_str!("../docs/api-reference.md");
const CONFIG_GUIDE_MD: &str = include_str!("../docs/configuration.md");
const WORKFLOWS_MD: &str = include_str!("../docs/workflows.md");
const QUICKSTART_MD: &str = include_str!("../docs/quickstart.md");

fn doc_resource(uri: &str, name: &str, description: &str, content: &str) -> Resource {
    let raw = RawResource {
        uri: uri.to_string(),
        name: name.to_string(),
        description: Some(description.to_string()),
        mime_type: Some("text/markdown".to_string()),
        size: Some(content.len() as u32),
    };
    Resource::new(raw, None)
}

fn doc_resource_template(uri_template: &str, name: &str, description: &str) -> ResourceTemplate {
    let raw = RawResourceTemplate {
        uri_template: uri_template.to_string(),
        name: name.to_string(),
        description: Some(description.to_string()),
        mime_type: Some("text/markdown".to_string()),
    };
    ResourceTemplate::new(raw, None)
}

#[tool_handler]
impl ServerHandler for DosBoxMcpServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo {
            instructions: Some(
                "MCP server for dosbox-automation REST API. Provides tools for controlling DOSBox instances including input injection, memory access, video capture, drive management, and Lua scripting.\n\n\
                **Documentation Resources** (read via `resources/read`):\n\
                - `docs://dosbox-mcp/api-reference` — Complete API reference for all 43 tools\n\
                - `docs://dosbox-mcp/configuration` — Server configuration guide\n\
                - `docs://dosbox-mcp/workflows` — Common workflow examples (game install, memory trainer, testing)\n\
                - `docs://dosbox-mcp/quickstart` — Quick start guide\n\n\
                **Prompts** (invoke via `prompts/get`):\n\
                - `install-game` — Step-by-step game installation from CD/ISO\n\
                - `memory-trainer` — Find and freeze memory values (lives, ammo, etc.)\n\
                - `automated-test` — Record/replay deterministic input sequences\n\
                - `lua-script` — Load and execute Lua automation scripts".into()
            ),
            capabilities: ServerCapabilities::builder()
                .enable_tools()
                .enable_resources()
                .enable_prompts()
                .build(),
            ..Default::default()
        }
    }

    // ---- Resources ----
    fn list_resources(
        &self,
        _request: Option<PaginatedRequestParam>,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<ListResourcesResult, McpErrorInternal>> + Send + '_ {
        let resources = vec![
            doc_resource(
                "docs://dosbox-mcp/api-reference",
                "API Reference",
                "Complete API reference for all 43 MCP tools with parameters, examples, and error codes",
                API_REFERENCE_MD,
            ),
            doc_resource(
                "docs://dosbox-mcp/configuration",
                "Configuration Guide",
                "Server configuration: config file, CLI options, environment variables, multi-instance setup",
                CONFIG_GUIDE_MD,
            ),
            doc_resource(
                "docs://dosbox-mcp/workflows",
                "Workflow Examples",
                "Common workflows: game installation, memory trainer, automated testing, Lua scripting",
                WORKFLOWS_MD,
            ),
            doc_resource(
                "docs://dosbox-mcp/quickstart",
                "Quick Start Guide",
                "Get running in 3 steps: install, configure MCP client, start server",
                QUICKSTART_MD,
            ),
        ];
        std::future::ready(Ok(ListResourcesResult {
            resources,
            next_cursor: None,
        }))
    }

    fn list_resource_templates(
        &self,
        _request: Option<PaginatedRequestParam>,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<ListResourceTemplatesResult, McpErrorInternal>> + Send + '_ {
        let templates = vec![
            doc_resource_template(
                "docs://dosbox-mcp/tool/{tool_name}",
                "Individual Tool Reference",
                "Get detailed reference for a specific tool by name (e.g., 'type_text', 'read_memory')",
            ),
        ];
        std::future::ready(Ok(ListResourceTemplatesResult {
            resource_templates: templates,
            next_cursor: None,
        }))
    }

    fn read_resource(
        &self,
        request: ReadResourceRequestParam,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<ReadResourceResult, McpErrorInternal>> + Send + '_ {
        let uri = request.uri.as_str();
        let contents = match uri {
            "docs://dosbox-mcp/api-reference" => ResourceContents::text(API_REFERENCE_MD, uri),
            "docs://dosbox-mcp/configuration" => ResourceContents::text(CONFIG_GUIDE_MD, uri),
            "docs://dosbox-mcp/workflows" => ResourceContents::text(WORKFLOWS_MD, uri),
            "docs://dosbox-mcp/quickstart" => ResourceContents::text(QUICKSTART_MD, uri),
            uri if uri.starts_with("docs://dosbox-mcp/tool/") => {
                let tool_name = uri.strip_prefix("docs://dosbox-mcp/tool/").unwrap_or("");
                let content = self.get_tool_doc(tool_name);
                ResourceContents::text(content, uri)
            }
            _ => {
                return std::future::ready(Err(McpErrorInternal::resource_not_found(
                    format!("Resource not found: {}", uri),
                    None,
                )));
            }
        };
        std::future::ready(Ok(ReadResourceResult {
            contents: vec![contents],
        }))
    }

    // ---- Prompts ----
    fn list_prompts(
        &self,
        _request: Option<PaginatedRequestParam>,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<ListPromptsResult, McpErrorInternal>> + Send + '_ {
        let prompts = vec![
            Prompt::new(
                "install-game",
                Some("Install a DOS game from CD/ISO image step by step"),
                Some(vec![
                    PromptArgument { name: "drive".into(), description: Some("Drive letter to mount (e.g., 'D')".into()), required: Some(true) },
                    PromptArgument { name: "image_path".into(), description: Some("Path to ISO/IMG disk image".into()), required: Some(true) },
                    PromptArgument { name: "install_cmd".into(), description: Some("Install command to run (e.g., 'd:\\install.exe')".into()), required: Some(false) },
                ]),
            ),
            Prompt::new(
                "memory-trainer",
                Some("Find a value in memory and freeze it (infinite lives, ammo, etc.)"),
                Some(vec![
                    PromptArgument { name: "value".into(), description: Some("Value to search for (e.g., current lives count)".into()), required: Some(true) },
                    PromptArgument { name: "width".into(), description: Some("Search width: 1=byte, 2=word, 4=dword".into()), required: Some(false) },
                    PromptArgument { name: "freeze_value".into(), description: Some("Value to freeze at (e.g., 99 for infinite lives)".into()), required: Some(true) },
                ]),
            ),
            Prompt::new(
                "automated-test",
                Some("Record input sequence, replay deterministically, verify frame"),
                Some(vec![
                    PromptArgument { name: "test_name".into(), description: Some("Name for this test".into()), required: Some(true) },
                    PromptArgument { name: "record_duration_secs".into(), description: Some("How long to record (seconds)".into()), required: Some(false) },
                ]),
            ),
            Prompt::new(
                "lua-script",
                Some("Load and execute a Lua automation script"),
                Some(vec![
                    PromptArgument { name: "script".into(), description: Some("Lua script source code".into()), required: Some(true) },
                    PromptArgument { name: "name".into(), description: Some("Optional name for the script".into()), required: Some(false) },
                ]),
            ),
        ];
        std::future::ready(Ok(ListPromptsResult {
            prompts,
            next_cursor: None,
        }))
    }

    fn get_prompt(
        &self,
        request: GetPromptRequestParam,
        _context: RequestContext<RoleServer>,
    ) -> impl Future<Output = Result<GetPromptResult, McpErrorInternal>> + Send + '_ {
        let name = request.name.as_str();
        let args = request.arguments.unwrap_or_default();

        let result = match name {
            "install-game" => self.prompt_install_game(&args),
            "memory-trainer" => self.prompt_memory_trainer(&args),
            "automated-test" => self.prompt_automated_test(&args),
            "lua-script" => self.prompt_lua_script(&args),
            _ => {
                return std::future::ready(Err(McpErrorInternal::method_not_found::<GetPromptRequestMethod>()));
            }
        };
        std::future::ready(Ok(result))
    }
}

impl DosBoxMcpServer {
    /// Get documentation for a specific tool by name
    fn get_tool_doc(&self, tool_name: &str) -> String {
        match tool_name {
            // Status
            "get_status" => r#"## get_status
Get current DOSBox status including program state and CPU info.

**Parameters:** None

**Returns:** `Status` object with emulator state, CPU info, and program info.

**Example:**
```json
{ "tool": "get_status", "arguments": {} }
```"#,
            "get_program_state" => r#"## get_program_state
Get the current program execution state.

**Parameters:** None

**Returns:** `ProgramState` with running program name, path, and state.

**Example:**
```json
{ "tool": "get_program_state", "arguments": {} }
```"#,
            "get_dosbox_info" => r#"## get_dosbox_info
Get DOSBox version and build information.

**Parameters:** None

**Returns:** `DosBoxInfo` with version, feature flags, binary path.

**Example:**
```json
{ "tool": "get_dosbox_info", "arguments": {} }
```"#,
            // Control
            "shutdown" => r#"## shutdown
Request graceful shutdown of DOSBox.

**Parameters:** None

**Returns:** `ControlResponse` with status.

**Example:**
```json
{ "tool": "shutdown", "arguments": {} }
```"#,
            "pause" => r#"## pause
Pause DOSBox emulation.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "pause", "arguments": {} }
```"#,
            "resume" => r#"## resume
Resume DOSBox emulation.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "resume", "arguments": {} }
```"#,
            "get_pause_state" => r#"## get_pause_state
Get current pause state.

**Parameters:** None

**Returns:** Boolean or object indicating paused state.

**Example:**
```json
{ "tool": "get_pause_state", "arguments": {} }
```"#,
            // CPU
            "get_registers" => r#"## get_registers
Get all CPU register values.

**Parameters:** None

**Returns:** `Registers` object with all general-purpose, segment, flag, and FPU registers.

**Example:**
```json
{ "tool": "get_registers", "arguments": {} }
```"#,
            "get_cycles" => r#"## get_cycles
Get current CPU cycles setting.

**Parameters:** None

**Returns:** `CpuCycles` with current cycles value.

**Example:**
```json
{ "tool": "get_cycles", "arguments": {} }
```"#,
            "set_cycles" => r#"## set_cycles
Set CPU cycles.

**Parameters:**
- `cycles` (string): Cycles value — numeric (e.g., "5000"), "auto", or "max"

**Returns:** `SetCyclesResponse`.

**Example:**
```json
{ "tool": "set_cycles", "arguments": { "cycles": "auto" } }
```"#,
            "set_register" => r#"## set_register
Set a CPU register value.

**Parameters:**
- `register` (string): Register name (e.g., "eax", "ebx", "eip", "cs", "ds")
- `value` (u64): Value to set

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "set_register", "arguments": { "register": "eax", "value": 0 } }
```"#,
            // Memory
            "read_memory" => r#"## read_memory
Read memory at offset (max 128 MiB per request). Returns base64-encoded data.

**Parameters:**
- `offset` (u64): Physical memory offset
- `length` (u32): Number of bytes to read (max 134,217,728)

**Returns:** `MemoryReadResponse` with base64 data.

**Example:**
```json
{ "tool": "read_memory", "arguments": { "offset": 0x100000, "length": 4096 } }
```"#,
            "write_memory" => r#"## write_memory
Write memory at offset (max 128 MiB per request). Data is base64-encoded.

**Parameters:**
- `offset` (u64): Physical memory offset
- `data` (Vec<u8>): Base64-encoded data to write (max 134,217,728 bytes)

**Returns:** `MemoryWriteResponse`.

**Example:**
```json
{ "tool": "write_memory", "arguments": { "offset": 0x100000, "data": "AQIDBA==" } }
```"#,
            "search_memory" => r#"## search_memory
Search memory for a value (byte/word/dword). Max 16 MiB per request.

**Parameters:**
- `value` (u64): Value to search for
- `width` (u8, default=1): Width in bytes: 1 (byte), 2 (word), 4 (dword)
- `start` (u64, optional): Start address (default: 0)
- `length` (u32, optional): Bytes to scan (max 16,777,216, default: 16 MiB)

**Returns:** `MemorySearchResponse` with array of matching addresses.

**Example:**
```json
{ "tool": "search_memory", "arguments": { "value": 3, "width": 1 } }
```"#,
            "allocate_memory" => r#"## allocate_memory
Allocate memory in DOSBox (CONV, UMB, XMS, EMS).

**Parameters:**
- `size` (u32): Size in bytes
- `area` (string, default="CONV"): Memory area — "CONV", "UMB", "XMS", "EMS"
- `strategy` (string, default="BEST_FIT"): Allocation strategy — "BEST_FIT", "FIRST_FIT", "LAST_FIT"

**Returns:** `MemoryAllocateResponse` with allocated address.

**Example:**
```json
{ "tool": "allocate_memory", "arguments": { "size": 65536, "area": "XMS" } }
```"#,
            "free_memory" => r#"## free_memory
Free previously allocated memory.

**Parameters:**
- `addr` (u64): Physical address of allocated block

**Returns:** `MemoryFreeResponse`.

**Example:**
```json
{ "tool": "free_memory", "arguments": { "addr": 0x100000 } }
```"#,
            "freeze_memory" => r#"## freeze_memory
Set a trainer-style value freeze (pins memory to fixed value each frame).

**Parameters:**
- `address` (u64): Physical address to freeze
- `value` (u64): Value to freeze
- `width` (u8, default=1): Width in bytes: 1, 2, or 4

**Returns:** `FreezeResponse`.

**Example:**
```json
{ "tool": "freeze_memory", "arguments": { "address": 65536, "value": 99, "width": 1 } }
```"#,
            "list_freezes" => r#"## list_freezes
List active value freezes.

**Parameters:** None

**Returns:** `ListFreezesResponse` with array of active freezes.

**Example:**
```json
{ "tool": "list_freezes", "arguments": {} }
```"#,
            "clear_freezes" => r#"## clear_freezes
Remove freezes. With no address, clears all freezes.

**Parameters:**
- `address` (u64, optional): Specific address to clear (omit to clear all)

**Returns:** `DeleteFreezesResponse`.

**Example:**
```json
{ "tool": "clear_freezes", "arguments": {} }
```"#,
            // Input
            "type_text" => r#"## type_text
Type text into DOSBox at specified characters per second (default: 30 cps).

**Parameters:**
- `text` (string): Text to type (supports escape sequences like \r, \n, \t)
- `cps` (f32, default=30): Characters per second

**Returns:** `TypeTextResponse`.

**Example:**
```json
{ "tool": "type_text", "arguments": { "text": "dir\r", "cps": 20 } }
```"#,
            "send_input_sequence" => r#"## send_input_sequence
Send a sequence of input events (keys, mouse, wheel) with optional timing.

**Parameters:**
- `events` (InputEvent[]): Array of input events

**InputEvent types:**
```json
{ "type": "key", "key": "KBD_enter", "pressed": true, "t": 0 }
{ "type": "mouse_move", "x_rel": 5.0, "y_rel": -3.0 }
{ "type": "mouse_button", "button": "left", "pressed": true }
{ "type": "mouse_wheel", "delta": 1.0 }
```

**Returns:** `InputSequenceResponse`.

**Example:**
```json
{ "tool": "send_input_sequence", "arguments": { "events": [{ "type": "key", "key": "KBD_enter", "pressed": true, "t": 0 }] } }
```"#,
            // Video
            "capture_frame" => r#"## capture_frame
Capture a frame (raw or rendered) as PNG/JPEG/raw. Returns base64-encoded image.

**Parameters:**
- `mode` (string, default="raw"): "raw" (emulator framebuffer) or "rendered" (post-shader)
- `format` (string, default="png"): "png", "jpeg", "raw"
- `quality` (u8, default=85): JPEG quality 1-100

**Returns:** `CaptureFrameResult` with base64 data, size, format, mode.

**Example:**
```json
{ "tool": "capture_frame", "arguments": { "mode": "rendered", "format": "png" } }
```"#,
            "get_frame_info" => r#"## get_frame_info
Get frame dimensions and format info.

**Parameters:** None

**Returns:** `FrameInfo` with width, height, bpp, pitch.

**Example:**
```json
{ "tool": "get_frame_info", "arguments": {} }
```"#,
            "read_screen_text" => r#"## read_screen_text
Read text-mode screen content (useful for driving installers).

**Parameters:** None

**Returns:** `ScreenText` with rows, cols, and text content.

**Example:**
```json
{ "tool": "read_screen_text", "arguments": {} }
```"#,
            // Recording
            "start_recording" => r#"## start_recording
Start recording input sequence.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "start_recording", "arguments": {} }
```"#,
            "pause_recording" => r#"## pause_recording
Pause/resume input recording.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "pause_recording", "arguments": {} }
```"#,
            "stop_recording" => r#"## stop_recording
Stop input recording and return recorded data.

**Parameters:** None

**Returns:** `RecordingResult` with events array.

**Example:**
```json
{ "tool": "stop_recording", "arguments": {} }
```"#,
            "get_recording_status" => r#"## get_recording_status
Get current recording status.

**Parameters:** None

**Returns:** `RecordingStatus` with recording state.

**Example:**
```json
{ "tool": "get_recording_status", "arguments": {} }
```"#,
            // Drive
            "swap_disk" => r#"## swap_disk
Swap floppy/CD image in a drive.

**Parameters:**
- `drive` (string): Drive letter (e.g., "A", "C", "D")
- `image` (string): Path to disk image

**Returns:** `DriveSwapResponse`.

**Example:**
```json
{ "tool": "swap_disk", "arguments": { "drive": "D", "image": "/path/to/game.iso" } }
```"#,
            "lock_mounts" => r#"## lock_mounts
Lock or unlock drive mounts.

**Parameters:**
- `lock` (boolean): Lock state

**Returns:** `MountLockResponse`.

**Example:**
```json
{ "tool": "lock_mounts", "arguments": { "lock": true } }
```"#,
            "get_mount_lock" => r#"## get_mount_lock
Get current mount lock state.

**Parameters:** None

**Returns:** `MountLockStatus`.

**Example:**
```json
{ "tool": "get_mount_lock", "arguments": {} }
```"#,
            // Script
            "load_script" => r#"## load_script
Load a Lua script.

**Parameters:**
- `script` (string): Lua script source code
- `name` (string, optional): Optional name for the script

**Returns:** `ScriptLoadResponse`.

**Example:**
```json
{ "tool": "load_script", "arguments": { "script": "print('hello')", "name": "test" } }
```"#,
            "start_script" => r#"## start_script
Start executing loaded Lua script.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "start_script", "arguments": {} }
```"#,
            "stop_script" => r#"## stop_script
Stop running Lua script.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "stop_script", "arguments": {} }
```"#,
            "get_script_status" => r#"## get_script_status
Get Lua script execution status.

**Parameters:** None

**Returns:** `ScriptStatus`.

**Example:**
```json
{ "tool": "get_script_status", "arguments": {} }
```"#,
            // Capture
            "start_video_capture" => r#"## start_video_capture
Start video capture (ZMBV).

**Parameters:**
- `mode` (string, default="raw"): "raw" or "rendered"

**Returns:** `CaptureStartResponse`.

**Example:**
```json
{ "tool": "start_video_capture", "arguments": { "mode": "rendered" } }
```"#,
            "stop_video_capture" => r#"## stop_video_capture
Stop video capture.

**Parameters:** None

**Returns:** `ControlResponse`.

**Example:**
```json
{ "tool": "stop_video_capture", "arguments": {} }
```"#,
            "get_capture_status" => r#"## get_capture_status
Get video capture status.

**Parameters:** None

**Returns:** `CaptureStatus`.

**Example:**
```json
{ "tool": "get_capture_status", "arguments": {} }
```"#,
            "get_compression" => r#"## get_compression
Get video capture compression settings.

**Parameters:** None

**Returns:** `CaptureCompression` with raw/rendered levels.

**Example:**
```json
{ "tool": "get_compression", "arguments": {} }
```"#,
            "set_compression" => r#"## set_compression
Set video capture compression settings.

**Parameters:**
- `raw` (u8, optional): Compression level for raw mode (0-9)
- `rendered` (u8, optional): Compression level for rendered mode (0-9)

**Returns:** `CaptureCompression`.

**Example:**
```json
{ "tool": "set_compression", "arguments": { "raw": 5, "rendered": 7 } }
```"#,
            // I/O
            "read_port" => r#"## read_port
Read I/O port (byte or word).

**Parameters:**
- `port` (u16): Port number (0-65535)
- `width` (u8, default=1): 1 for byte, 2 for word

**Returns:** `PortReadResponse`.

**Example:**
```json
{ "tool": "read_port", "arguments": { "port": 0x3F8, "width": 1 } }
```"#,
            "write_port" => r#"## write_port
Write I/O port (byte or word).

**Parameters:**
- `port` (u16): Port number (0-65535)
- `value` (u32): Value to write
- `width` (u8, default=1): 1 for byte, 2 for word

**Returns:** `PortWriteResponse`.

**Example:**
```json
{ "tool": "write_port", "arguments": { "port": 0x3F8, "value": 0x80, "width": 1 } }
```"#,
            // DOS
            "get_dos_internals" => r#"## get_dos_internals
Get DOS internals (MCB map, etc.).

**Parameters:** None

**Returns:** `DosInternals` with MCB chain, DOS structures.

**Example:**
```json
{ "tool": "get_dos_internals", "arguments": {} }
```"#,
            _ => "No documentation available for this tool",
        }.to_string()
    }

    // ---- Prompt Implementations ----
    fn prompt_install_game(&self, args: &serde_json::Map<String, serde_json::Value>) -> GetPromptResult {
        let drive = args.get("drive").and_then(|v| v.as_str()).unwrap_or("D");
        let image_path = args.get("image_path").and_then(|v| v.as_str()).unwrap_or("/path/to/game.iso");
        let install_cmd = args.get("install_cmd").and_then(|v| v.as_str()).unwrap_or("d:\\\\install.exe");

        let messages = vec![
            PromptMessage::new_text(PromptMessageRole::User, format!(
                "Install a DOS game from CD/ISO using the dosbox-mcp tools.\n\n\
                **Steps:**\n\
                1. Mount the disk image to drive {}\n\
                2. Run the installer\n\
                3. Navigate installer menus via text input\n\
                4. Launch the game\n\n\
                **Tools to use:**\n\
                - `swap_disk` — mount the ISO\n\
                - `type_text` — send keystrokes\n\
                - `read_screen_text` — read installer prompts\n\
                - `capture_frame` — verify visual state\n\n\
                **Example sequence:**",
                drive
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, format!(
                "{{\"tool\": \"swap_disk\", \"arguments\": {{\"drive\": \"{}\", \"image\": \"{}\" }}}}",
                drive, image_path
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, format!(
                "{{\"tool\": \"type_text\", \"arguments\": {{\"text\": \"{}\\r\", \"cps\": 20 }}}}",
                install_cmd
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"read_screen_text\", \"arguments\": {}}"),
            PromptMessage::new_text(PromptMessageRole::User, "Continue sending keystrokes based on installer prompts. Use `read_screen_text` after each step to see the current screen."),
        ];
        GetPromptResult {
            description: Some("Install a DOS game from CD/ISO".into()),
            messages,
        }
    }

    fn prompt_memory_trainer(&self, args: &serde_json::Map<String, serde_json::Value>) -> GetPromptResult {
        let value = args.get("value").and_then(|v| v.as_u64()).unwrap_or(3);
        let width = args.get("width").and_then(|v| v.as_u64()).unwrap_or(1) as u8;
        let freeze_value = args.get("freeze_value").and_then(|v| v.as_u64()).unwrap_or(99);

        let messages = vec![
            PromptMessage::new_text(PromptMessageRole::User, format!(
                "Set up a memory trainer (freeze a value like lives, ammo, score).\n\n\
                **Steps:**\n\
                1. Search memory for the current value ({})\n\
                2. Identify the correct address (may need multiple searches)\n\
                3. Freeze that address to the desired value ({})\n\
                4. Verify it works\n\
                5. Clear freezes when done\n\n\
                **Tools:** `search_memory`, `freeze_memory`, `list_freezes`, `clear_freezes`",
                value, freeze_value
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, format!(
                "{{\"tool\": \"search_memory\", \"arguments\": {{\"value\": {}, \"width\": {}}}}}",
                value, width
            )),
            PromptMessage::new_text(PromptMessageRole::User, format!(
                "Review the returned addresses. If multiple matches, change the value in-game (e.g., lose a life) and search again with the new value to narrow down. Once you have the correct address, freeze it:"
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, format!(
                "{{\"tool\": \"freeze_memory\", \"arguments\": {{\"address\": <ADDRESS_FROM_SEARCH>, \"value\": {}, \"width\": {}}}}}",
                freeze_value, width
            )),
            PromptMessage::new_text(PromptMessageRole::User, "Verify the freeze works in-game. When finished, clear all freezes:"),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"clear_freezes\", \"arguments\": {}}"),
        ];
        GetPromptResult {
            description: Some("Memory trainer: find and freeze a value".into()),
            messages,
        }
    }

    fn prompt_automated_test(&self, args: &serde_json::Map<String, serde_json::Value>) -> GetPromptResult {
        let test_name = args.get("test_name").and_then(|v| v.as_str()).unwrap_or("test");
        let _duration = args.get("record_duration_secs").and_then(|v| v.as_u64()).unwrap_or(30);

        let messages = vec![
            PromptMessage::new_text(PromptMessageRole::User, format!(
                "Create an automated test: record input, replay, verify.\n\n\
                **Test:** {}\n\n\
                **Workflow:**\n\
                1. Start recording\n\
                2. Perform actions in DOSBox\n\
                3. Stop recording (save the events)\n\
                4. Reset game state\n\
                5. Replay with `send_input_sequence`\n\
                6. Capture frame and compare\n\n\
                **Tools:** `start_recording`, `stop_recording`, `send_input_sequence`, `capture_frame`",
                test_name
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"start_recording\", \"arguments\": {}}"),
            PromptMessage::new_text(PromptMessageRole::User, "Perform your test actions in DOSBox now..."),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"stop_recording\", \"arguments\": {}}"),
            PromptMessage::new_text(PromptMessageRole::User, "Save the returned `events` array. Then reset the game state and replay:"),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"send_input_sequence\", \"arguments\": {\"events\": \"<SAVED_EVENTS_ARRAY>\"}}}"),
            PromptMessage::new_text(PromptMessageRole::User, "Verify the result:"),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"capture_frame\", \"arguments\": {\"mode\": \"raw\", \"format\": \"png\"}}}"),
        ];
        GetPromptResult {
            description: Some("Automated test: record/replay/verify".into()),
            messages,
        }
    }

    fn prompt_lua_script(&self, args: &serde_json::Map<String, serde_json::Value>) -> GetPromptResult {
        let script = args.get("script").and_then(|v| v.as_str()).unwrap_or("print('Hello from Lua!')");
        let name = args.get("name").and_then(|v| v.as_str()).unwrap_or("script");

        let messages = vec![
            PromptMessage::new_text(PromptMessageRole::User, format!(
                "Load and execute a Lua automation script.\n\n\
                **Script name:** {}\n\n\
                **Script:**\n```lua\n{}\n```\n\n\
                **Tools:** `load_script`, `start_script`, `get_script_status`, `stop_script`",
                name, script
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, format!(
                "{{\"tool\": \"load_script\", \"arguments\": {{\"script\": \"{}\", \"name\": \"{}\" }}}}",
                script.replace('\n', "\\n").replace('"', "\\\""),
                name
            )),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"start_script\", \"arguments\": {}}"),
            PromptMessage::new_text(PromptMessageRole::User, "Monitor execution:"),
            PromptMessage::new_text(PromptMessageRole::Assistant, "{\"tool\": \"get_script_status\", \"arguments\": {}}"),
        ];
        GetPromptResult {
            description: Some("Load and run a Lua script".into()),
            messages,
        }
    }
}