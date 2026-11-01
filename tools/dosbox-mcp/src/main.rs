use anyhow::Result;
use clap::Parser;
use config::{Config, File, Environment};
use dirs::config_dir;
use rmcp::{ServiceExt, transport::io::stdio};
use std::path::PathBuf;
use std::sync::Arc;
use tracing::{error, info};
use tracing_subscriber::{EnvFilter, fmt};

mod client;
mod cfg;
mod dosbox_launcher;
mod error;
mod server;
mod types;

use crate::cfg::McpConfig;
use crate::server::DosBoxMcpServer;
use crate::dosbox_launcher::DosBoxLauncher;

#[derive(Parser, Debug, Clone)]
#[command(name = "dosbox-mcp", version, about = "MCP server for dosbox-automation REST API")]
struct Args {
    /// Base URL of dosbox-automation API (e.g., http://localhost:8386)
    #[arg(long, env = "DOSBOX_API_URL")]
    base_url: Option<String>,

    /// Request timeout in seconds (default: 30)
    #[arg(long, default_value = "30")]
    timeout_secs: u64,

    /// Disable TLS verification (for self-signed certs)
    #[arg(long)]
    no_verify_tls: bool,

    /// Config file path (default: ~/.config/dosbox-mcp/config.toml)
    #[arg(long)]
    config: Option<PathBuf>,

    /// Log level (trace, debug, info, warn, error)
    #[arg(long, default_value = "info")]
    log_level: String,

    /// Path to dosbox-automation binary (auto-detect if not specified)
    #[arg(long, env = "DOSBOX_AUTOMATION_BIN")]
    dosbox_bin: Option<PathBuf>,

    /// Auto-start dosbox-automation if not running (default: true)
    #[arg(long, default_value = "true")]
    auto_start: bool,

    /// Don't auto-start dosbox-automation
    #[arg(long, conflicts_with = "auto_start")]
    no_auto_start: bool,

    /// Working directory for dosbox-automation
    #[arg(long)]
    working_dir: Option<PathBuf>,

    /// Extra arguments to pass to dosbox-automation
    #[arg(last = true)]
    dosbox_args: Vec<String>,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();

    // Determine auto-start setting
    let auto_start = args.auto_start && !args.no_auto_start;

    // Initialize tracing
    let filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new(args.log_level.clone()));
    fmt().with_env_filter(filter).init();

    info!("Starting dosbox-mcp server");

    // Load configuration
    let config = load_config(args.clone())?;

    // Create dosbox launcher if auto-start is enabled
    let dosbox_launcher = if auto_start {
        let launcher = DosBoxLauncher::new(
            args.dosbox_bin.clone(),
            args.working_dir.clone(),
            args.dosbox_args.clone(),
            config.base_url.clone(),
        )?;
        Some(launcher)
    } else {
        None
    };

    // Start dosbox-automation if launcher is configured
    let base_url = if let Some(ref launcher) = dosbox_launcher {
        info!("Starting dosbox-automation...");
        launcher.start().await?
    } else {
        config.base_url.clone()
    };

    // Create the dosbox client
    let client = Arc::new(client::DosBoxClient::new(McpConfig {
        base_url: base_url.clone(),
        timeout_secs: config.timeout_secs,
        verify_tls: config.verify_tls,
    })?);

    // Create and run the MCP server with stdio transport
    let server = DosBoxMcpServer::new(client);
    let service = server.serve(stdio()).await?;

    // Wait for the server to finish
    service.waiting().await?;

    // Stop dosbox-automation if we started it
    if let Some(launcher) = dosbox_launcher {
        info!("Stopping dosbox-automation...");
        launcher.stop().await?;
    }

    Ok(())
}

fn load_config(args: Args) -> Result<McpConfig> {
    let mut builder = Config::builder();

    // 1. Config file (lowest priority)
    let config_path = args.config.or_else(|| {
        config_dir().map(|dir| dir.join("dosbox-mcp").join("config.toml"))
    });

    if let Some(path) = &config_path {
        if path.exists() {
            builder = builder.add_source(File::from(path.clone()).required(false));
            info!("Loading config from: {:?}", path);
        }
    }

    // 2. Environment variables
    builder = builder.add_source(Environment::with_prefix("DOSBOX_MCP").separator("_"));

    // 3. CLI args (highest priority) - manually override after building
    let mut config: McpConfig = builder.build()?.try_deserialize()?;

    // Apply CLI overrides
    if let Some(url) = args.base_url {
        config.base_url = url;
    }
    config.timeout_secs = args.timeout_secs;
    config.verify_tls = !args.no_verify_tls;

    // Validate
    if config.base_url.trim().is_empty() {
        anyhow::bail!("base_url is required (use --base-url or DOSBOX_API_URL env var)");
    }

    info!("Configuration loaded: base_url={}, timeout={}s, verify_tls={}",
          config.base_url, config.timeout_secs, config.verify_tls);

    Ok(config)
}