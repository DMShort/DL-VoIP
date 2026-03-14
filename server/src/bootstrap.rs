use sqlx::PgPool;
use tracing::{error, info};

use crate::auth::password;
use crate::config::BootstrapConfig;
use crate::db;

/// Run first-time bootstrap if no organizations exist.
/// Creates the initial org, admin user, and default "General" channel.
pub async fn run_bootstrap(pool: &PgPool, config: &BootstrapConfig) -> anyhow::Result<()> {
    // Check if any orgs already exist
    if db::organizations::any_exist(pool).await? {
        info!("Organizations exist — skipping bootstrap");
        return Ok(());
    }

    info!("No organizations found — entering bootstrap mode");

    // Validate required bootstrap config
    let org_name = config.org_name.as_deref().filter(|s| !s.is_empty());
    let org_tag = config.org_tag.as_deref().filter(|s| !s.is_empty());
    let admin_username = config.admin_username.as_deref().filter(|s| !s.is_empty());
    let admin_password = config.admin_password.as_deref().filter(|s| !s.is_empty());

    let (org_name, org_tag, admin_username, admin_password) =
        match (org_name, org_tag, admin_username, admin_password) {
            (Some(name), Some(tag), Some(user), Some(pass)) => (name, tag, user, pass),
            _ => {
                error!("Bootstrap failed: missing required environment variables.");
                error!("Set the following variables and restart:");
                error!("  VOIP__BOOTSTRAP__ORG_NAME    — Organization name (e.g., \"My Family\")");
                error!("  VOIP__BOOTSTRAP__ORG_TAG     — Organization tag (e.g., \"family\")");
                error!(
                    "  VOIP__BOOTSTRAP__ADMIN_USERNAME — Admin username (e.g., \"admin\")"
                );
                error!("  VOIP__BOOTSTRAP__ADMIN_PASSWORD — Initial admin password");
                anyhow::bail!("Bootstrap configuration incomplete");
            }
        };

    // Create organization
    let org = db::organizations::create_org(pool, org_name, org_tag).await?;
    info!("Created organization: \"{}\" (tag: {})", org.name, org.tag);

    // Create admin user
    let hash = password::hash_password(admin_password)?;
    let admin = db::users::create_user(pool, org.id, admin_username, &hash, None, true).await?;
    info!("Created admin user: \"{}\"", admin.username);

    // Set org owner
    db::organizations::set_owner(pool, org.id, admin.id).await?;

    // Create default "General" channel
    sqlx::query(
        r#"
        INSERT INTO channels (org_id, name, description, position)
        VALUES ($1, 'General', 'Default voice channel', 0)
        "#,
    )
    .bind(org.id)
    .execute(pool)
    .await?;
    info!("Created default channel: \"General\"");

    info!("Bootstrap complete!");
    Ok(())
}
