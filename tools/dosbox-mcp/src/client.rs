use crate::cfg::McpConfig;
use crate::error::{McpError, Result};
use reqwest::{Client, ClientBuilder, Response};
use serde::de::DeserializeOwned;
use std::time::Duration;
use tracing::{debug, error};

#[derive(Clone)]
pub struct DosBoxClient {
    client: Client,
    base_url: String,
    timeout: Duration,
}

impl DosBoxClient {
    pub fn new(config: McpConfig) -> Result<Self> {
        let timeout = Duration::from_secs(config.timeout_secs);

        let client = ClientBuilder::new()
            .timeout(timeout)
            .danger_accept_invalid_certs(!config.verify_tls)
            .pool_idle_timeout(Duration::from_secs(90))
            .pool_max_idle_per_host(10)
            .build()
            .map_err(McpError::Connection)?;

        let base_url = config.base_url.trim_end_matches('/').to_string();

        Ok(Self {
            client,
            base_url,
            timeout,
        })
    }

    /// Build the full URL for an API endpoint
    fn url(&self, path: &str) -> Result<String> {
        let path = path.trim_start_matches('/');
        let url = format!("{}/api/v1/{}", self.base_url, path);
        Ok(url)
    }

    /// Execute a GET request and parse JSON response
    pub async fn get<T: DeserializeOwned>(&self, path: &str) -> Result<T> {
        let url = self.url(path)?;
        debug!("GET {}", url);

        let response = self.client.get(&url).send().await?;
        self.handle_response(response).await
    }

    /// Execute a POST request with JSON body and parse JSON response
    pub async fn post<T: DeserializeOwned, B: serde::Serialize>(
        &self,
        path: &str,
        body: &B,
    ) -> Result<T> {
        let url = self.url(path)?;
        debug!("POST {} with body: {}", url, serde_json::to_string(body).unwrap_or_default());

        let response = self.client.post(&url).json(body).send().await?;
        self.handle_response(response).await
    }

    /// Execute a PUT request with JSON body and parse JSON response
    pub async fn put<T: DeserializeOwned, B: serde::Serialize>(
        &self,
        path: &str,
        body: &B,
    ) -> Result<T> {
        let url = self.url(path)?;
        debug!("PUT {} with body: {}", url, serde_json::to_string(body).unwrap_or_default());

        let response = self.client.put(&url).json(body).send().await?;
        self.handle_response(response).await
    }

    /// Execute a DELETE request (no body)
    pub async fn delete<T: DeserializeOwned>(&self, path: &str) -> Result<T> {
        let url = self.url(path)?;
        debug!("DELETE {}", url);

        let response = self.client.delete(&url).send().await?;
        self.handle_response(response).await
    }

    /// Execute a DELETE request with JSON body
    pub async fn delete_with_body<T: DeserializeOwned, B: serde::Serialize>(&self, path: &str, body: &B) -> Result<T> {
        let url = self.url(path)?;
        debug!("DELETE {} with body: {}", url, serde_json::to_string(body).unwrap_or_default());

        let response = self.client.delete(&url).json(body).send().await?;
        self.handle_response(response).await
    }

    /// Execute a POST request that returns raw bytes (for frame capture)
    pub async fn post_bytes<B: serde::Serialize>(&self, path: &str, body: &B) -> Result<Vec<u8>> {
        let url = self.url(path)?;
        debug!("POST {} (bytes)", url);

        let response = self.client.post(&url).json(body).send().await?;
        let status = response.status();

        if !status.is_success() {
            let text = response.text().await.unwrap_or_default();
            error!("API error {}: {}", status, text);
            return Err(McpError::Http {
                status: status.as_u16(),
                message: text,
            });
        }

        let bytes = response.bytes().await.map_err(McpError::Connection)?;
        Ok(bytes.to_vec())
    }

    /// Execute a GET request that returns raw bytes
    pub async fn get_bytes(&self, path: &str) -> Result<Vec<u8>> {
        let url = self.url(path)?;
        debug!("GET {} (bytes)", url);

        let response = self.client.get(&url).send().await?;
        let status = response.status();

        if !status.is_success() {
            let text = response.text().await.unwrap_or_default();
            error!("API error {}: {}", status, text);
            return Err(McpError::Http {
                status: status.as_u16(),
                message: text,
            });
        }

        let bytes = response.bytes().await.map_err(McpError::Connection)?;
        Ok(bytes.to_vec())
    }

    async fn handle_response<T: DeserializeOwned>(&self, response: Response) -> Result<T> {
        let status = response.status();

        if status.is_success() {
            let body = response.json().await?;
            Ok(body)
        } else {
            let text = response.text().await.unwrap_or_else(|_| "Unknown error".to_string());
            error!("API error {}: {}", status, text);
            Err(McpError::Http {
                status: status.as_u16(),
                message: text,
            })
        }
    }

    /// Health check - ping the server
    pub async fn health_check(&self) -> Result<()> {
        self.get::<serde_json::Value>("/status").await?;
        Ok(())
    }
}