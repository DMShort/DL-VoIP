use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::{IntoResponse, Json},
};
use serde::Deserialize;
use serde_json::json;

use crate::auth::password;
use crate::db;
use crate::server::AppState;
use crate::server::messages;

use super::middleware::AdminAuth;

#[derive(Deserialize)]
pub struct CreateChannelRequest {
    pub name: String,
    pub description: Option<String>,
    pub parent_id: Option<i32>,
    pub max_users: Option<i32>,
    pub position: Option<i32>,
    pub password: Option<String>,
}

#[derive(Deserialize)]
pub struct UpdateChannelRequest {
    pub name: Option<String>,
    pub description: Option<String>,
    pub max_users: Option<i32>,
    pub position: Option<i32>,
}

pub async fn list_channels(
    State(state): State<AppState>,
    auth: AdminAuth,
) -> impl IntoResponse {
    match db::channels::list_by_org(&state.pool, auth.org_id).await {
        Ok(channels) => Json(json!({"channels": channels})).into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to list channels: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn create_channel(
    State(state): State<AppState>,
    auth: AdminAuth,
    Json(body): Json<CreateChannelRequest>,
) -> impl IntoResponse {
    if body.name.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Channel name is required"})),
        )
            .into_response();
    }

    let password_hash = match &body.password {
        Some(pw) if !pw.is_empty() => match password::hash_password(pw) {
            Ok(h) => Some(h),
            Err(e) => {
                return (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(json!({"error": format!("Failed to hash password: {}", e)})),
                )
                    .into_response();
            }
        },
        _ => None,
    };

    match db::channels::create(
        &state.pool,
        auth.org_id,
        &body.name,
        body.description.as_deref(),
        body.parent_id,
        body.max_users.unwrap_or(0),
        body.position.unwrap_or(0),
        password_hash.as_deref(),
    )
    .await
    {
        Ok(channel) => {
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "create_channel",
                Some("channel"),
                Some(channel.id),
                Some(json!({"name": channel.name})),
                None,
            )
            .await;

            // Broadcast channel list update to all connected users in the org
            let channel_info = messages::ChannelInfo {
                id: channel.id,
                name: channel.name.clone(),
                description: channel.description.clone(),
                parent_id: channel.parent_id,
                position: channel.position.unwrap_or(0),
                has_password: channel.password_hash.is_some(),
                max_users: channel.max_users.unwrap_or(0),
                user_count: 0,
            };
            let envelope = messages::Envelope::new("channel_updated", &messages::ChannelUpdatedPayload { channel: channel_info });
            state.connection_manager.broadcast_to_org(auth.org_id, &envelope.to_json());

            (StatusCode::CREATED, Json(json!({"channel": channel}))).into_response()
        }
        Err(e) => {
            let msg = e.to_string();
            if msg.contains("duplicate") || msg.contains("unique") {
                (
                    StatusCode::CONFLICT,
                    Json(json!({"error": "Channel name already exists"})),
                )
                    .into_response()
            } else {
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(json!({"error": format!("Failed to create channel: {}", e)})),
                )
                    .into_response()
            }
        }
    }
}

pub async fn update_channel(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(channel_id): Path<i32>,
    Json(body): Json<UpdateChannelRequest>,
) -> impl IntoResponse {
    // Verify channel belongs to same org
    let existing = match db::channels::get_by_id(&state.pool, channel_id).await {
        Ok(Some(ch)) if ch.org_id == auth.org_id => ch,
        Ok(Some(_)) => {
            return (
                StatusCode::FORBIDDEN,
                Json(json!({"error": "Channel belongs to a different organization"})),
            )
                .into_response();
        }
        Ok(None) => {
            return (
                StatusCode::NOT_FOUND,
                Json(json!({"error": "Channel not found"})),
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

    let name = body.name.as_deref().unwrap_or(&existing.name);
    let description = body.description.as_deref().or(existing.description.as_deref());
    let max_users = body.max_users.unwrap_or(existing.max_users.unwrap_or(0));
    let position = body.position.unwrap_or(existing.position.unwrap_or(0));

    match db::channels::update(&state.pool, channel_id, name, description, max_users, position).await {
        Ok(channel) => {
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "update_channel",
                Some("channel"),
                Some(channel_id),
                Some(json!({"name": channel.name})),
                None,
            )
            .await;

            // Update in-memory limit
            state.channel_manager.set_limit(channel_id, max_users);

            // Broadcast update
            let user_count = state.channel_manager.member_count(channel_id);
            let channel_info = messages::ChannelInfo {
                id: channel.id,
                name: channel.name.clone(),
                description: channel.description.clone(),
                parent_id: channel.parent_id,
                position: channel.position.unwrap_or(0),
                has_password: channel.password_hash.is_some(),
                max_users: channel.max_users.unwrap_or(0),
                user_count: user_count as i32,
            };
            let envelope = messages::Envelope::new("channel_updated", &messages::ChannelUpdatedPayload { channel: channel_info });
            state.connection_manager.broadcast_to_org(auth.org_id, &envelope.to_json());

            Json(json!({"channel": channel})).into_response()
        }
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to update channel: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn delete_channel(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(channel_id): Path<i32>,
) -> impl IntoResponse {
    // Verify channel belongs to same org
    match db::channels::get_by_id(&state.pool, channel_id).await {
        Ok(Some(ch)) if ch.org_id == auth.org_id => {}
        Ok(Some(_)) => {
            return (
                StatusCode::FORBIDDEN,
                Json(json!({"error": "Channel belongs to a different organization"})),
            )
                .into_response();
        }
        Ok(None) => {
            return (
                StatusCode::NOT_FOUND,
                Json(json!({"error": "Channel not found"})),
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

    // Notify users in the channel before removing them
    let members = state.channel_manager.members(channel_id);
    if !members.is_empty() {
        let envelope = messages::Envelope::new("channel_deleted", &messages::ChannelDeletedPayload { channel_id });
        state.connection_manager.broadcast_to_channel(channel_id, &envelope.to_json(), None);

        // Remove all users from the channel
        for user_id in &members {
            state.channel_manager.leave(channel_id, *user_id);
            state.connection_manager.remove_channel(*user_id, channel_id);
        }
    }

    // Remove from in-memory state
    state.channel_manager.remove_channel(channel_id);

    // Delete from database
    match db::channels::delete(&state.pool, channel_id).await {
        Ok(_) => {
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "delete_channel",
                Some("channel"),
                Some(channel_id),
                None,
                None,
            )
            .await;

            Json(json!({"status": "ok"})).into_response()
        }
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to delete channel: {}", e)})),
        )
            .into_response(),
    }
}
