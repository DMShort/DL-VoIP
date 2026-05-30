use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::{IntoResponse, Json},
};
use chrono::Utc;
use rand::Rng;
use serde::Deserialize;
use serde_json::json;

use crate::db;
use crate::server::AppState;

use super::middleware::AdminAuth;

const INVITE_CHARSET: &[u8] = b"ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no ambiguous 0/O/1/I

fn generate_code() -> String {
    let mut rng = rand::thread_rng();
    (0..8)
        .map(|_| INVITE_CHARSET[rng.gen_range(0..INVITE_CHARSET.len())] as char)
        .collect()
}

#[derive(Deserialize)]
pub struct CreateInviteRequest {
    pub max_uses: Option<i32>,
    pub expires_in_hours: Option<i32>,
}

pub async fn create_invite(
    State(state): State<AppState>,
    auth: AdminAuth,
    Json(body): Json<CreateInviteRequest>,
) -> impl IntoResponse {
    let expires_at = body.expires_in_hours.map(|h| {
        Utc::now() + chrono::Duration::hours(h as i64)
    });

    let code = generate_code();

    match db::invites::create_invite(
        &state.pool,
        auth.org_id,
        auth.user_id,
        &code,
        body.max_uses,
        expires_at,
    )
    .await
    {
        Ok(invite) => (StatusCode::CREATED, Json(json!({"invite": invite}))).into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to create invite: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn list_invites(
    State(state): State<AppState>,
    auth: AdminAuth,
) -> impl IntoResponse {
    match db::invites::list_by_org(&state.pool, auth.org_id).await {
        Ok(invites) => Json(json!({"invites": invites})).into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to list invites: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn revoke_invite(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(code): Path<String>,
) -> impl IntoResponse {
    match db::invites::delete_by_code(&state.pool, auth.org_id, &code).await {
        Ok(true) => Json(json!({"status": "ok"})).into_response(),
        Ok(false) => (
            StatusCode::NOT_FOUND,
            Json(json!({"error": "Invite not found"})),
        )
            .into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to revoke invite: {}", e)})),
        )
            .into_response(),
    }
}
