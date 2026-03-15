use axum::{
    extract::State,
    http::{header, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Router,
};
use tokio::fs;
use tracing::info;

use super::AppState;

/// Serves the latest client zip from the `downloads/` directory.
/// Looks for files matching `DadLink-*.zip` and serves the first one found.
async fn download_client(State(_state): State<AppState>) -> Response {
    let download_dir = std::path::Path::new("downloads");

    if !download_dir.exists() {
        return (StatusCode::NOT_FOUND, "No downloads available.").into_response();
    }

    // Find the first zip file in downloads/
    let mut entries = match fs::read_dir(download_dir).await {
        Ok(entries) => entries,
        Err(_) => return (StatusCode::INTERNAL_SERVER_ERROR, "Cannot read downloads directory.").into_response(),
    };

    let mut zip_path = None;
    while let Ok(Some(entry)) = entries.next_entry().await {
        let name = entry.file_name().to_string_lossy().to_string();
        if name.ends_with(".zip") {
            zip_path = Some(entry.path());
            break;
        }
    }

    let path = match zip_path {
        Some(p) => p,
        None => return (StatusCode::NOT_FOUND, "No client download available.").into_response(),
    };

    let filename = path.file_name().unwrap().to_string_lossy().to_string();

    match fs::read(&path).await {
        Ok(data) => {
            info!("Serving client download: {} ({} bytes)", filename, data.len());
            (
                StatusCode::OK,
                [
                    (header::CONTENT_TYPE, "application/zip".to_string()),
                    (header::CONTENT_DISPOSITION, format!("attachment; filename=\"{}\"", filename)),
                ],
                data,
            ).into_response()
        }
        Err(_) => (StatusCode::INTERNAL_SERVER_ERROR, "Failed to read file.").into_response(),
    }
}

/// Simple HTML page with a download button.
async fn download_page(State(state): State<AppState>) -> Response {
    let version = state.config.server.client_version.as_deref().unwrap_or("latest");
    let html = format!(r#"<!DOCTYPE html>
<html>
<head>
    <title>DadLink Client Download</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #1a1a2e; color: #eee; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }}
        .card {{ background: #16213e; border-radius: 12px; padding: 48px; text-align: center; box-shadow: 0 8px 32px rgba(0,0,0,0.3); max-width: 400px; }}
        h1 {{ color: #e94560; margin: 0 0 8px; }}
        .version {{ color: #888; margin-bottom: 24px; }}
        .btn {{ display: inline-block; background: #e94560; color: white; text-decoration: none; padding: 14px 32px; border-radius: 8px; font-size: 18px; font-weight: bold; transition: background 0.2s; }}
        .btn:hover {{ background: #c73550; }}
        .note {{ color: #666; font-size: 13px; margin-top: 24px; }}
    </style>
</head>
<body>
    <div class="card">
        <h1>DadLink</h1>
        <p class="version">Version {version} &mdash; Windows 64-bit</p>
        <a href="/download/client" class="btn">Download Client</a>
        <p class="note">Extract the zip and run dadlink-client.exe</p>
    </div>
</body>
</html>"#);

    (
        StatusCode::OK,
        [(header::CONTENT_TYPE, "text/html; charset=utf-8".to_string())],
        html,
    ).into_response()
}

pub fn routes() -> Router<AppState> {
    Router::new()
        .route("/download", get(download_page))
        .route("/download/client", get(download_client))
}
