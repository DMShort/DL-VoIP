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

use super::middleware::AdminAuth;

#[derive(Deserialize)]
pub struct CreateUserRequest {
    pub username: String,
    pub password: String,
    pub display_name: Option<String>,
    pub is_admin: Option<bool>,
}

#[derive(Deserialize)]
pub struct UpdateUserRequest {
    pub display_name: Option<String>,
    pub is_admin: Option<bool>,
    pub is_active: Option<bool>,
    pub password: Option<String>,
}

pub async fn list_users(
    State(state): State<AppState>,
    auth: AdminAuth,
) -> impl IntoResponse {
    match db::users::list_by_org(&state.pool, auth.org_id).await {
        Ok(users) => Json(json!({"users": users})).into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to list users: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn get_user(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(user_id): Path<i32>,
) -> impl IntoResponse {
    match db::users::find_by_id(&state.pool, user_id).await {
        Ok(Some(user)) if user.org_id == auth.org_id => {
            let roles = db::roles::get_user_roles(&state.pool, user_id)
                .await
                .unwrap_or_default();
            Json(json!({"user": user, "roles": roles})).into_response()
        }
        Ok(Some(_)) => (
            StatusCode::FORBIDDEN,
            Json(json!({"error": "User belongs to a different organization"})),
        )
            .into_response(),
        Ok(None) => (
            StatusCode::NOT_FOUND,
            Json(json!({"error": "User not found"})),
        )
            .into_response(),
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Database error: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn create_user(
    State(state): State<AppState>,
    auth: AdminAuth,
    Json(body): Json<CreateUserRequest>,
) -> impl IntoResponse {
    // Validate input
    if body.username.is_empty() || body.password.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Username and password are required"})),
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

    // Hash password
    let hash = match password::hash_password(&body.password) {
        Ok(h) => h,
        Err(e) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"error": format!("Failed to hash password: {}", e)})),
            )
                .into_response();
        }
    };

    match db::users::create_user(
        &state.pool,
        auth.org_id,
        &body.username,
        &hash,
        body.display_name.as_deref(),
        body.is_admin.unwrap_or(false),
    )
    .await
    {
        Ok(user) => {
            // Audit log
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "create_user",
                Some("user"),
                Some(user.id),
                Some(json!({"username": user.username})),
                None,
            )
            .await;

            (StatusCode::CREATED, Json(json!({"user": user}))).into_response()
        }
        Err(e) => {
            let msg = e.to_string();
            if msg.contains("duplicate") || msg.contains("unique") {
                (
                    StatusCode::CONFLICT,
                    Json(json!({"error": "Username already exists in this organization"})),
                )
                    .into_response()
            } else {
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(json!({"error": format!("Failed to create user: {}", e)})),
                )
                    .into_response()
            }
        }
    }
}

pub async fn update_user(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(user_id): Path<i32>,
    Json(body): Json<UpdateUserRequest>,
) -> impl IntoResponse {
    // Verify user exists and belongs to same org
    let user = match db::users::find_by_id(&state.pool, user_id).await {
        Ok(Some(u)) if u.org_id == auth.org_id => u,
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
    };

    // Build update query dynamically
    let display_name = body.display_name.as_deref().or(user.display_name.as_deref());
    let is_admin = body.is_admin.unwrap_or(user.is_admin.unwrap_or(false));
    let is_active = body.is_active.unwrap_or(user.is_active.unwrap_or(true));

    // Handle optional password change
    let password_hash = if let Some(ref new_password) = body.password {
        if new_password.len() < 8 {
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({"error": "Password must be at least 8 characters"})),
            )
                .into_response();
        }
        match password::hash_password(new_password) {
            Ok(h) => Some(h),
            Err(e) => {
                return (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(json!({"error": format!("Failed to hash password: {}", e)})),
                )
                    .into_response();
            }
        }
    } else {
        None
    };

    let result = if let Some(ref hash) = password_hash {
        sqlx::query_as::<_, crate::db::models::User>(
            r#"
            UPDATE users SET display_name = $2, is_admin = $3, is_active = $4, password_hash = $5
            WHERE id = $1
            RETURNING *
            "#,
        )
        .bind(user_id)
        .bind(display_name)
        .bind(is_admin)
        .bind(is_active)
        .bind(hash)
        .fetch_one(&state.pool)
        .await
    } else {
        sqlx::query_as::<_, crate::db::models::User>(
            r#"
            UPDATE users SET display_name = $2, is_admin = $3, is_active = $4
            WHERE id = $1
            RETURNING *
            "#,
        )
        .bind(user_id)
        .bind(display_name)
        .bind(is_admin)
        .bind(is_active)
        .fetch_one(&state.pool)
        .await
    };

    match result {
        Ok(updated) => {
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "update_user",
                Some("user"),
                Some(user_id),
                Some(json!({"username": updated.username})),
                None,
            )
            .await;

            // If user was deactivated, revoke all their sessions
            if !is_active {
                let _ = db::sessions::revoke_all_for_user(&state.pool, user_id).await;
                state.session_cache.invalidate_all();
                // Disconnect from WebSocket if connected
                if let Some(handle) = state.connection_manager.get(user_id) {
                    let _ = handle.tx.send(
                        crate::server::messages::make_error("session_revoked", "Your account has been deactivated"),
                    );
                }
                state.connection_manager.remove(user_id);
            }

            Json(json!({"user": updated})).into_response()
        }
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to update user: {}", e)})),
        )
            .into_response(),
    }
}

pub async fn bulk_create_users(
    State(state): State<AppState>,
    auth: AdminAuth,
    Json(body): Json<Vec<CreateUserRequest>>,
) -> impl IntoResponse {
    if body.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Request body must be a non-empty array"})),
        )
            .into_response();
    }

    let mut created = Vec::new();
    let mut failed: Vec<serde_json::Value> = Vec::new();

    for req in body {
        if req.username.is_empty() || req.password.is_empty() {
            failed.push(json!({"username": req.username, "error": "Username and password are required"}));
            continue;
        }
        if req.password.len() < 8 {
            failed.push(json!({"username": req.username, "error": "Password must be at least 8 characters"}));
            continue;
        }

        let hash = match password::hash_password(&req.password) {
            Ok(h) => h,
            Err(_) => {
                failed.push(json!({"username": req.username, "error": "Failed to hash password"}));
                continue;
            }
        };

        match db::users::create_user(
            &state.pool,
            auth.org_id,
            &req.username,
            &hash,
            req.display_name.as_deref(),
            req.is_admin.unwrap_or(false),
        )
        .await
        {
            Ok(user) => {
                let _ = db::audit::log_action(
                    &state.pool,
                    auth.org_id,
                    auth.user_id,
                    "create_user",
                    Some("user"),
                    Some(user.id),
                    Some(json!({"username": user.username, "bulk": true})),
                    None,
                )
                .await;
                created.push(user);
            }
            Err(e) => {
                let msg = e.to_string();
                let reason = if msg.contains("duplicate") || msg.contains("unique") {
                    "Username already exists".to_string()
                } else {
                    format!("Database error: {}", e)
                };
                failed.push(json!({"username": req.username, "error": reason}));
            }
        }
    }

    let status = if created.is_empty() && !failed.is_empty() {
        StatusCode::UNPROCESSABLE_ENTITY
    } else {
        StatusCode::CREATED
    };

    (status, Json(json!({"created": created, "failed": failed}))).into_response()
}

pub async fn delete_user(
    State(state): State<AppState>,
    auth: AdminAuth,
    Path(user_id): Path<i32>,
) -> impl IntoResponse {
    // Cannot delete yourself
    if user_id == auth.user_id {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "Cannot delete your own account"})),
        )
            .into_response();
    }

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

    // Revoke sessions and disconnect
    let _ = db::sessions::revoke_all_for_user(&state.pool, user_id).await;
    state.session_cache.invalidate_all();
    state.connection_manager.remove(user_id);
    state.presence.remove(user_id);
    state.srtp_manager.remove(user_id);

    // Deactivate rather than hard delete (preserves audit trail)
    match sqlx::query("UPDATE users SET is_active = FALSE WHERE id = $1")
        .bind(user_id)
        .execute(&state.pool)
        .await
    {
        Ok(_) => {
            let _ = db::audit::log_action(
                &state.pool,
                auth.org_id,
                auth.user_id,
                "delete_user",
                Some("user"),
                Some(user_id),
                None,
                None,
            )
            .await;

            Json(json!({"status": "ok"})).into_response()
        }
        Err(e) => (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"error": format!("Failed to delete user: {}", e)})),
        )
            .into_response(),
    }
}
