use chrono::{Duration, Utc};
use jsonwebtoken::{decode, encode, DecodingKey, EncodingKey, Header, Validation};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Claims {
    pub sub: i32,       // user_id
    pub org: i32,       // org_id
    pub roles: Vec<i32>, // role IDs
    pub exp: i64,       // expiration timestamp
    pub iat: i64,       // issued at timestamp
    pub jti: String,    // unique token ID (UUID)
}

/// Create a new JWT token for a user.
pub fn create_token(
    user_id: i32,
    org_id: i32,
    role_ids: Vec<i32>,
    secret: &str,
    expiration_secs: u64,
) -> anyhow::Result<(String, Uuid, chrono::DateTime<Utc>, chrono::DateTime<Utc>)> {
    let now = Utc::now();
    let jti = Uuid::new_v4();
    let expires_at = now + Duration::seconds(expiration_secs as i64);

    let claims = Claims {
        sub: user_id,
        org: org_id,
        roles: role_ids,
        exp: expires_at.timestamp(),
        iat: now.timestamp(),
        jti: jti.to_string(),
    };

    let token = encode(
        &Header::default(), // HS256
        &claims,
        &EncodingKey::from_secret(secret.as_bytes()),
    )?;

    Ok((token, jti, now, expires_at))
}

/// Validate a JWT token and return the claims.
pub fn validate_token(token: &str, secret: &str) -> anyhow::Result<Claims> {
    let token_data = decode::<Claims>(
        token,
        &DecodingKey::from_secret(secret.as_bytes()),
        &Validation::default(),
    )?;

    Ok(token_data.claims)
}
