use axum::{
    extract::State,
    http::{header, StatusCode},
    response::{Html, IntoResponse},
    routing::{get, post},
    Json, Router,
};
use serde::Deserialize;

use crate::auth::{jwt, password};
use crate::db;

use super::AppState;

#[derive(Deserialize)]
pub struct LoginRequest {
    pub username: String,
    pub password: String,
    pub org_tag: String,
}

/// REST login endpoint — returns a JWT token for use with the admin API.
pub async fn api_login(
    State(state): State<AppState>,
    Json(req): Json<LoginRequest>,
) -> impl IntoResponse {
    // Find user by username + org
    let user = match db::users::find_by_username_and_org(&state.pool, &req.username, &req.org_tag)
        .await
    {
        Ok(Some(u)) => u,
        _ => {
            return (
                StatusCode::UNAUTHORIZED,
                Json(serde_json::json!({"error": "Invalid username or password"})),
            )
                .into_response()
        }
    };

    // Verify password
    let valid = password::verify_password(&req.password, &user.password_hash).unwrap_or(false);
    if !valid {
        return (
            StatusCode::UNAUTHORIZED,
            Json(serde_json::json!({"error": "Invalid username or password"})),
        )
            .into_response();
    }

    // Check admin status
    let is_admin = user.is_admin.unwrap_or(false);
    if !is_admin {
        // Check role-based admin
        let has_admin_role = sqlx::query_scalar::<_, i32>(
            "SELECT COALESCE(BIT_OR(r.permissions), 0) FROM user_roles ur JOIN roles r ON r.id = ur.role_id WHERE ur.user_id = $1",
        )
        .bind(user.id)
        .fetch_one(&state.pool)
        .await
        .unwrap_or(0);

        if has_admin_role & 128 == 0 {
            // 128 = ADMIN permission bit
            return (
                StatusCode::FORBIDDEN,
                Json(serde_json::json!({"error": "Admin access required"})),
            )
                .into_response();
        }
    }

    // Get role IDs
    let role_ids = sqlx::query_scalar::<_, i32>(
        "SELECT role_id FROM user_roles WHERE user_id = $1",
    )
    .bind(user.id)
    .fetch_all(&state.pool)
    .await
    .unwrap_or_default();

    // Create JWT
    let (token, jti, issued_at, expires_at) = match jwt::create_token(
        user.id,
        user.org_id,
        role_ids,
        &state.config.security.jwt_secret,
        state.config.security.jwt_expiration_secs,
    ) {
        Ok(t) => t,
        Err(_) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(serde_json::json!({"error": "Failed to create token"})),
            )
                .into_response()
        }
    };

    // Save session
    let _ = db::sessions::create_session(
        &state.pool,
        user.id,
        jti,
        issued_at,
        expires_at,
        None,
    )
    .await;

    (
        StatusCode::OK,
        Json(serde_json::json!({
            "token": token,
            "user_id": user.id,
            "username": user.username,
            "is_admin": true,
        })),
    )
        .into_response()
}

/// Serve the embedded admin panel HTML.
pub async fn admin_panel() -> impl IntoResponse {
    Html(include_str!("../../static/admin.html"))
}

/// Serve embedded CSS.
pub async fn admin_css() -> impl IntoResponse {
    (
        [(header::CONTENT_TYPE, "text/css")],
        include_str!("../../static/admin.css"),
    )
}

/// Serve embedded JS.
pub async fn admin_js() -> impl IntoResponse {
    (
        [(header::CONTENT_TYPE, "application/javascript")],
        include_str!("../../static/admin.js"),
    )
}

pub fn web_admin_routes() -> Router<AppState> {
    Router::new()
        .route("/", get(admin_panel))
        .route("/static/admin.css", get(admin_css))
        .route("/static/admin.js", get(admin_js))
        .route("/api/login", post(api_login))
}
