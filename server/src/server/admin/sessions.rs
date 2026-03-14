use axum::{
    extract::{Path, Query, State},
    http::StatusCode,
    response::{IntoResponse, Json},
};
use serde::Deserialize;
use serde_json::json;

use crate::db;
use crate::server::AppState;

use super::middleware::AdminAuth;

pub async fn revoke_session(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(session_id): Path<i32>,
) -> impl IntoResponse {
    // Find the session to verify org ownership
    let session = match sqlx::query_as::<_, crate::db::models::Session>(
        "SELECT * FROM sessions WHERE id = $1",
    )
    .bind(session_id)
    .fetch_optional(&state.pool)
    .await
    {
        Ok(Some(s)) => s,
        Ok(None) => {
            return (
                StatusCode::NOT_FOUND,
                Json(json!({"error": "Session not found"})),
            )
                .into_response();
        }
        Err(e) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": format!("Database error: {}", e)})),
            )
                .into_response();
        }
    };

    // Verify the session's user belongs to same org
    match db::users::find_by_id(&state.pool, session.user_id).await {
        Ok(Some(u)) if u.org_id == auth.org_id => {}
        _ => {
            return (
                StatusCode::FORBIDDEN,
                Json(json!({"error": "Session belongs to a different organization"})),
            )
                .into_response();
        }
    }

    // Revoke the session
    if let Err(e) = db::sessions::revoke_session(&state.pool, session.token_jti).await {
        return (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to revoke session: {}", e)})),
        )
            .into_response();
    }

    // Invalidate cache — use the JTI directly
    state.session_cache.invalidate(session.token_jti);

    let _ = db::audit::log_action(
        &state.pool,
        auth.org_id,
        auth.user_id,
        "revoke_session",
        Some("session"),
        Some(session_id),
        Some(json!({"target_user_id": session.user_id})),
        None,
    )
    .await;

    Json(json!({"status": "ok"})).into_response()
}

pub async fn revoke_user_sessions(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(user_id): Path<i32>,
) -> impl IntoResponse {
    // Verify user belongs to same org
    match db::users::find_by_id(&state.pool, user_id).await {
        Ok(Some(u)) if u.org_id == auth.org_id => {}
        Ok(Some(_)) => {
            return (
                StatusCode::FORBIDDEN,
                Json(json!({"error": "User belongs to a different organization"})),
            )
                .into_response();
        }
        Ok(None) => {
            return (
                StatusCode::NOT_FOUND,
                Json(json!({"error": "User not found"})),
            )
                .into_response();
        }
        Err(e) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": format!("Database error: {}", e)})),
            )
                .into_response();
        }
    }

    if let Err(e) = db::sessions::revoke_all_for_user(&state.pool, user_id).await {
        return (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to revoke sessions: {}", e)})),
        )
            .into_response();
    }

    state.session_cache.invalidate_all();

    // Disconnect WebSocket if connected
    if let Some(handle) = state.connection_manager.get(user_id) {
        let _ = handle.tx.send(
            crate::server::messages::make_error("session_revoked", "All sessions have been revoked"),
        );
    }
    state.connection_manager.remove(user_id);

    let _ = db::audit::log_action(
        &state.pool,
        auth.org_id,
        auth.user_id,
        "revoke_all_sessions",
        Some("user"),
        Some(user_id),
        None,
        None,
    )
    .await;

    Json(json!({"status": "ok"})).into_response()
}

#[derive(Deserialize)]
pub struct AuditQuery {
    pub limit: Option<i64>,
    pub offset: Option<i64>,
}

pub async fn get_audit_log(
    State(state): State<AppState>,
    auth: AdminAuth,
    Query(query): Query<AuditQuery>,
) -> impl IntoResponse {
    let limit = query.limit.unwrap_or(50).min(200);
    let offset = query.offset.unwrap_or(0);

    match db::audit::list_recent(&state.pool, auth.org_id, limit, offset).await {
        Ok(entries) => Json(json!({"entries": entries, "limit": limit, "offset": offset})).into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to fetch audit log: {}", e)})),
        )
            .into_response(),
    }
}
