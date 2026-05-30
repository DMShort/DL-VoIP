use axum::{
    extract::{ConnectInfo, State},
    http::StatusCode,
    response::{IntoResponse, Json},
};
use chrono::Utc;
use serde::Deserialize;
use serde_json::json;
use std::net::SocketAddr;

use crate::auth::password;
use crate::db;
use crate::server::AppState;

#[derive(Deserialize)]
pub struct RegisterRequest {
    pub invite_code: String,
    pub username: String,
    pub password: String,
    pub display_name: Option<String>,
}

pub async fn register(
    State(state): State<AppState>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    Json(body): Json<RegisterRequest>,
) -> impl IntoResponse {
    // Basic input validation
    if body.username.is_empty() || body.password.is_empty() || body.invite_code.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "invite_code, username, and password are required"})),
        )
            .into_response();
    }

    if body.password.len() < 8 {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Password must be at least 8 characters"})),
        )
            .into_response();
    }

    if body.username.len() > 64 {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Username must be 64 characters or fewer"})),
        )
            .into_response();
    }

    // Look up invite
    let invite = match db::invites::find_by_code(&state.pool, &body.invite_code).await {
        Ok(Some(inv)) => inv,
        Ok(None) => {
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({"error": "Invalid invite code"})),
            )
                .into_response();
        }
        Err(_) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": "Internal error"})),
            )
                .into_response();
        }
    };

    // Check expiry
    if let Some(expires_at) = invite.expires_at {
        if Utc::now() > expires_at {
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({"error": "Invite code has expired"})),
            )
                .into_response();
        }
    }

    // Check use limit
    if let Some(max) = invite.max_uses {
        if invite.use_count >= max {
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({"error": "Invite code has reached its maximum uses"})),
            )
                .into_response();
        }
    }

    // Hash password
    let hash = match password::hash_password(&body.password) {
        Ok(h) => h,
        Err(_) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": "Internal error"})),
            )
                .into_response();
        }
    };

    // Create user
    let user = match db::users::create_user(
        &state.pool,
        invite.org_id,
        &body.username,
        &hash,
        body.display_name.as_deref(),
        false, // new registrations are never admin
    )
    .await
    {
        Ok(u) => u,
        Err(e) => {
            let msg = e.to_string();
            if msg.contains("duplicate") || msg.contains("unique") {
                return (
                    StatusCode::CONFLICT,
                    Json(json!({"error": "Username already exists in this organization"})),
                )
                    .into_response();
            }
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": "Failed to create user"})),
            )
                .into_response();
        }
    };

    // Increment invite use count
    let _ = db::invites::increment_use(&state.pool, &body.invite_code).await;

    // Audit log
    let ip = addr.ip().to_string();
    let _ = db::audit::log_action(
        &state.pool,
        invite.org_id,
        user.id,
        "register",
        Some("user"),
        Some(user.id),
        Some(json!({"invite_code": body.invite_code})),
        Some(&ip),
    )
    .await;

    (
        StatusCode::CREATED,
        Json(json!({
            "user_id": user.id,
            "username": user.username,
            "display_name": user.display_name,
        })),
    )
        .into_response()
}
