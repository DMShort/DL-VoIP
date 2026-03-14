use dashmap::DashMap;

#[derive(Debug, Clone, Default)]
pub struct UserPresence {
    pub muted: bool,
    pub deafened: bool,
    pub talking: bool,
}

/// Tracks per-user presence state (muted, deafened, talking).
pub struct PresenceManager {
    states: DashMap<i32, UserPresence>,
}

impl PresenceManager {
    pub fn new() -> Self {
        Self {
            states: DashMap::new(),
        }
    }

    pub fn get(&self, user_id: i32) -> UserPresence {
        self.states
            .get(&user_id)
            .map(|s| s.clone())
            .unwrap_or_default()
    }

    pub fn set_muted(&self, user_id: i32, muted: bool) -> UserPresence {
        let mut entry = self.states.entry(user_id).or_default();
        entry.muted = muted;
        entry.clone()
    }

    pub fn set_deafened(&self, user_id: i32, deafened: bool) -> UserPresence {
        let mut entry = self.states.entry(user_id).or_default();
        entry.deafened = deafened;
        entry.clone()
    }

    pub fn set_talking(&self, user_id: i32, talking: bool) -> UserPresence {
        let mut entry = self.states.entry(user_id).or_default();
        entry.talking = talking;
        entry.clone()
    }

    pub fn remove(&self, user_id: i32) {
        self.states.remove(&user_id);
    }
}
