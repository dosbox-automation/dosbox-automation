use thiserror::Error;

#[derive(Error, Debug)]
pub enum McpError {
    #[error("HTTP error: {status} - {message}")]
    Http { status: u16, message: String },

    #[error("Request timeout after {secs}s")]
    Timeout { secs: u64 },

    #[error("Connection error: {0}")]
    Connection(#[from] reqwest::Error),

    #[error("JSON parse error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("URL parse error: {0}")]
    Url(#[from] url::ParseError),

    #[error("Configuration error: {0}")]
    Config(String),

    #[error("API error: {endpoint} - {detail}")]
    Api { endpoint: String, detail: String },

    #[error("Invalid parameter: {field} - {reason}")]
    InvalidParam { field: String, reason: String },

    #[error("Internal error: {0}")]
    Internal(String),
}

impl McpError {
    pub fn api(endpoint: &str, detail: &str) -> Self {
        Self::Api {
            endpoint: endpoint.to_string(),
            detail: detail.to_string(),
        }
    }

    pub fn invalid_param(field: &str, reason: &str) -> Self {
        Self::InvalidParam {
            field: field.to_string(),
            reason: reason.to_string(),
        }
    }
}

pub type Result<T> = std::result::Result<T, McpError>;