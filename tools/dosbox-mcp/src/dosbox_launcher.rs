use anyhow::{Result, Context};
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::Arc;
use std::time::Duration;
use tokio::process::Command;
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout};
use tracing::{debug, error, info, warn};
use url::Url;

/// Launcher for dosbox-automation process
pub struct DosBoxLauncher {
    binary_path: PathBuf,
    working_dir: Option<PathBuf>,
    extra_args: Vec<String>,
    base_url: String,
    child: Arc<Mutex<Option<tokio::process::Child>>>,
    lib_path: Option<PathBuf>,
}

impl DosBoxLauncher {
    pub fn new(
        binary_path: Option<PathBuf>,
        working_dir: Option<PathBuf>,
        extra_args: Vec<String>,
        base_url: String,
    ) -> Result<Self> {
        let binary_path = if let Some(path) = binary_path {
            path
        } else {
            Self::find_dosbox_binary()?
        };

        // Find the libiir.so library path
        let lib_path = Self::find_libiir_path(&binary_path);

        Ok(Self {
            binary_path,
            working_dir,
            extra_args,
            base_url,
            child: Arc::new(Mutex::new(None)),
            lib_path,
        })
    }

    fn find_dosbox_binary() -> Result<PathBuf> {
        // Try common locations
        let candidates = [
            // Build directory (development)
            "/home/papa/projects/dosbox-automation-notes/dosbox-automation-is-not-your-nanny/build/release-linux/dosbox",
            // System install
            "/usr/local/bin/dosbox-automation",
            "/usr/bin/dosbox-automation",
            // User install
            &format!("{}/.local/bin/dosbox-automation", std::env::var("HOME").unwrap_or_default()),
            // PATH
            "dosbox-automation",
            "dosbox",
        ];

        for candidate in &candidates {
            let path = PathBuf::from(candidate);
            if path.exists() || which::which(candidate).is_ok() {
                info!("Found dosbox-automation at: {}", candidate);
                return Ok(path);
            }
        }

        anyhow::bail!("dosbox-automation binary not found. Please specify with --dosbox-bin or DOSBOX_AUTOMATION_BIN env var");
    }

    fn find_libiir_path(binary_path: &Path) -> Option<PathBuf> {
        // Try to find libiir.so relative to the binary
        if let Some(parent) = binary_path.parent() {
            let candidates = [
                parent.join("_deps/iir1-build"),
                parent.join("../_deps/iir1-build"),
                parent.join("../../_deps/iir1-build"),
            ];
            for candidate in &candidates {
                if candidate.join("libiir.so").exists() {
                    return Some(candidate.clone());
                }
            }
        }
        None
    }

    pub async fn start(&self) -> Result<String> {
        // Check if server is already running
        if self.is_server_ready().await {
            info!("dosbox-automation already running at {}", self.base_url);
            return Ok(self.base_url.clone());
        }

        info!("Starting dosbox-automation from: {:?}", self.binary_path);

        // Parse base URL to get port
        let url = Url::parse(&self.base_url)
            .context("Invalid base_url")?;
        let port = url.port().unwrap_or(8386);
        let host = url.host_str().unwrap_or("127.0.0.1");

        // Build command
        let mut cmd = Command::new(&self.binary_path);
        
        // Set working directory
        if let Some(ref wd) = self.working_dir {
            cmd.current_dir(wd);
        }

        // Set LD_LIBRARY_PATH if we found libiir
        if let Some(ref lib_path) = self.lib_path {
            let mut lib_path_env = std::env::var("LD_LIBRARY_PATH").unwrap_or_default();
            if !lib_path_env.is_empty() {
                lib_path_env.push(':');
            }
            lib_path_env.push_str(&lib_path.to_string_lossy());
            cmd.env("LD_LIBRARY_PATH", lib_path_env);
        }

        // Configure webserver via command line args
        cmd.arg("--set")
            .arg("webserver_enabled=on");
        cmd.arg("--set")
            .arg(format!("webserver_bind_address={}", host));
        cmd.arg("--set")
            .arg(format!("webserver_port={}", port));
        cmd.arg("--set")
            .arg("webserver_allow_remote=off");
        cmd.arg("--set")
            .arg("webserver_token_file=off");

        // Add extra args
        for arg in &self.extra_args {
            cmd.arg(arg);
        }

        // Run headless (no GUI) for server mode
        cmd.arg("--noprimaryconf")
            .arg("--noautoexec")
            .arg("--securemode");

        // Configure stdio
        cmd.stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .stdin(Stdio::null());

        // Spawn the process
        let child = cmd.spawn()
            .context("Failed to spawn dosbox-automation")?;

        let pid = child.id().unwrap_or(0);
        info!("Started dosbox-automation (PID: {})", pid);

        // Store child process
        {
            let mut child_guard = self.child.lock().await;
            *child_guard = Some(child);
        }

        // Wait for server to be ready
        self.wait_for_server(port).await?;

        Ok(self.base_url.clone())
    }

    async fn is_server_ready(&self) -> bool {
        let client = reqwest::Client::new();
        let url = format!("{}/api/v1/dosbox/info", self.base_url);
        client.get(&url).send().await.map(|r| r.status().is_success()).unwrap_or(false)
    }

    async fn wait_for_server(&self, port: u16) -> Result<()> {
        let max_wait = Duration::from_secs(30);
        let start = std::time::Instant::now();
        
        while start.elapsed() < max_wait {
            if self.is_server_ready().await {
                info!("dosbox-automation webserver ready at {}", self.base_url);
                return Ok(());
            }
            debug!("Waiting for dosbox-automation webserver on port {}...", port);
            sleep(Duration::from_millis(500)).await;
        }

        anyhow::bail!("Timeout waiting for dosbox-automation webserver on port {}", port);
    }

    pub async fn stop(&self) -> Result<()> {
        let mut child_guard = self.child.lock().await;
        if let Some(mut child) = child_guard.take() {
            info!("Stopping dosbox-automation (PID: {:?})...", child.id());
            
            // Try graceful shutdown first
            child.kill().await.ok();
            
            // Wait for process to exit
            let _ = timeout(Duration::from_secs(5), child.wait()).await;
            
            info!("dosbox-automation stopped");
        } else {
            warn!("No dosbox-automation process to stop");
        }
        Ok(())
    }
}

impl Drop for DosBoxLauncher {
    fn drop(&mut self) {
        // Best effort cleanup - spawn a task to kill the process
        if let Some(child) = self.child.blocking_lock().take() {
            let _ = tokio::runtime::Handle::current().spawn(async move {
                let mut child = child;
                let _ = child.kill().await;
                let _ = timeout(Duration::from_secs(3), child.wait()).await;
            });
        }
    }
}