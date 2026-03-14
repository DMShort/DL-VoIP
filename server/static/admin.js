// DadLink Server Admin Panel

let token = '';
let currentUser = '';

// --- Auth ---

document.getElementById('login-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const errEl = document.getElementById('login-error');
    errEl.style.display = 'none';

    const body = {
        username: document.getElementById('login-username').value,
        password: document.getElementById('login-password').value,
        org_tag: document.getElementById('login-org').value,
    };

    try {
        const res = await fetch('/api/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        const data = await res.json();
        if (!res.ok) {
            errEl.textContent = data.error || 'Login failed';
            errEl.style.display = 'block';
            return;
        }
        token = data.token;
        currentUser = data.username;
        document.getElementById('admin-username').textContent = currentUser;
        document.getElementById('login-screen').style.display = 'none';
        document.getElementById('admin-dashboard').style.display = 'flex';
        loadDashboard();
    } catch (err) {
        errEl.textContent = 'Connection failed. Is the server running?';
        errEl.style.display = 'block';
    }
});

document.getElementById('logout-btn').addEventListener('click', () => {
    token = '';
    document.getElementById('admin-dashboard').style.display = 'none';
    document.getElementById('login-screen').style.display = 'flex';
    document.getElementById('login-password').value = '';
});

// --- API Helper ---

async function api(method, path, body) {
    const opts = {
        method,
        headers: {
            'Authorization': 'Bearer ' + token,
            'Content-Type': 'application/json',
        },
    };
    if (body) opts.body = JSON.stringify(body);
    const res = await fetch(path, opts);
    if (res.status === 401) {
        alert('Session expired. Please log in again.');
        document.getElementById('logout-btn').click();
        return null;
    }
    return res.json();
}

// --- Navigation ---

document.querySelectorAll('.nav-links a').forEach(link => {
    link.addEventListener('click', (e) => {
        e.preventDefault();
        const tab = link.dataset.tab;
        document.querySelectorAll('.nav-links a').forEach(l => l.classList.remove('active'));
        link.classList.add('active');
        document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
        document.getElementById('tab-' + tab).classList.add('active');

        if (tab === 'dashboard') loadDashboard();
        else if (tab === 'users') loadUsers();
        else if (tab === 'channels') loadChannels();
        else if (tab === 'roles') loadRoles();
        else if (tab === 'audit') loadAudit();
    });
});

// --- Dashboard ---

async function loadDashboard() {
    try {
        const health = await fetch('/health').then(r => r.json());
        document.getElementById('stat-status').textContent = health.status === 'healthy' ? 'Healthy' : 'Degraded';
        document.getElementById('stat-status').style.color = health.status === 'healthy' ? '#4caf50' : '#f44336';

        const secs = health.uptime_seconds || 0;
        const h = Math.floor(secs / 3600);
        const m = Math.floor((secs % 3600) / 60);
        document.getElementById('stat-uptime').textContent = h + 'h ' + m + 'm';
    } catch {
        document.getElementById('stat-status').textContent = 'Offline';
        document.getElementById('stat-status').style.color = '#f44336';
    }

    const users = await api('GET', '/admin/users');
    if (users) document.getElementById('stat-users').textContent = users.users.length;

    const channels = await api('GET', '/admin/channels');
    if (channels) document.getElementById('stat-channels').textContent = channels.channels.length;
}

// --- Users ---

async function loadUsers() {
    const data = await api('GET', '/admin/users');
    if (!data) return;
    const tbody = document.querySelector('#users-table tbody');
    tbody.innerHTML = data.users.map(u => `
        <tr>
            <td>${u.id}</td>
            <td>${esc(u.username)}</td>
            <td>${esc(u.display_name || '--')}</td>
            <td>${u.is_admin ? '<span class="badge badge-blue">Admin</span>' : ''}</td>
            <td>${u.is_active !== false ? '<span class="badge badge-green">Active</span>' : '<span class="badge badge-red">Inactive</span>'}</td>
            <td>
                <button class="btn-icon" onclick="showEditUser(${u.id})" title="Edit">&#9998;</button>
                <button class="btn-icon danger" onclick="deleteUser(${u.id}, '${esc(u.username)}')" title="Delete">&#10005;</button>
                <button class="btn-icon danger" onclick="revokeSessions(${u.id})" title="Revoke Sessions">&#128274;</button>
            </td>
        </tr>
    `).join('');
}

function showCreateUser() {
    openModal('Create User', `
        <div class="form-group"><label>Username</label><input type="text" id="m-username" required></div>
        <div class="form-group"><label>Password (min 8 chars)</label><input type="password" id="m-password" required></div>
        <div class="form-group"><label>Display Name</label><input type="text" id="m-display-name"></div>
        <div class="checkbox-row"><input type="checkbox" id="m-is-admin"><label for="m-is-admin">Administrator</label></div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Create', cls: 'btn-primary', action: createUser },
    ]);
}

async function createUser() {
    const body = {
        username: document.getElementById('m-username').value,
        password: document.getElementById('m-password').value,
        display_name: document.getElementById('m-display-name').value || null,
        is_admin: document.getElementById('m-is-admin').checked,
    };
    if (body.password.length < 8) { alert('Password must be at least 8 characters'); return; }
    const res = await api('POST', '/admin/users', body);
    if (res) { closeModal(); loadUsers(); }
}

async function showEditUser(userId) {
    const data = await api('GET', '/admin/users/' + userId);
    if (!data) return;
    const u = data.user;
    openModal('Edit User: ' + u.username, `
        <div class="form-group"><label>Display Name</label><input type="text" id="m-display-name" value="${esc(u.display_name || '')}"></div>
        <div class="form-group"><label>New Password (leave blank to keep)</label><input type="password" id="m-password"></div>
        <div class="checkbox-row"><input type="checkbox" id="m-is-admin" ${u.is_admin ? 'checked' : ''}><label for="m-is-admin">Administrator</label></div>
        <div class="checkbox-row"><input type="checkbox" id="m-is-active" ${u.is_active !== false ? 'checked' : ''}><label for="m-is-active">Active</label></div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Save', cls: 'btn-primary', action: () => updateUser(userId) },
    ]);
}

async function updateUser(userId) {
    const body = {
        display_name: document.getElementById('m-display-name').value || null,
        is_admin: document.getElementById('m-is-admin').checked,
        is_active: document.getElementById('m-is-active').checked,
    };
    const pw = document.getElementById('m-password').value;
    if (pw) {
        if (pw.length < 8) { alert('Password must be at least 8 characters'); return; }
        body.password = pw;
    }
    const res = await api('PUT', '/admin/users/' + userId, body);
    if (res) { closeModal(); loadUsers(); }
}

async function deleteUser(userId, username) {
    if (!confirm('Delete user "' + username + '"? This cannot be undone.')) return;
    await api('DELETE', '/admin/users/' + userId);
    loadUsers();
}

async function revokeSessions(userId) {
    if (!confirm('Revoke all sessions for this user? They will be disconnected immediately.')) return;
    await api('DELETE', '/admin/users/' + userId + '/sessions');
    alert('Sessions revoked.');
}

// --- Channels ---

async function loadChannels() {
    const data = await api('GET', '/admin/channels');
    if (!data) return;
    const tbody = document.querySelector('#channels-table tbody');
    tbody.innerHTML = data.channels.map(c => `
        <tr>
            <td>${c.id}</td>
            <td>${esc(c.name)}</td>
            <td>${esc(c.description || '--')}</td>
            <td>${c.max_users || 'Unlimited'}</td>
            <td>${c.position || 0}</td>
            <td>
                <button class="btn-icon" onclick="showEditChannel(${c.id})" title="Edit">&#9998;</button>
                <button class="btn-icon danger" onclick="deleteChannel(${c.id}, '${esc(c.name)}')" title="Delete">&#10005;</button>
            </td>
        </tr>
    `).join('');
}

function showCreateChannel() {
    openModal('Create Channel', `
        <div class="form-group"><label>Name</label><input type="text" id="m-ch-name" required></div>
        <div class="form-group"><label>Description</label><input type="text" id="m-ch-desc"></div>
        <div class="form-group"><label>Max Users (0 = unlimited)</label><input type="number" id="m-ch-max" value="0" min="0"></div>
        <div class="form-group"><label>Position</label><input type="number" id="m-ch-pos" value="0" min="0"></div>
        <div class="form-group"><label>Password (optional)</label><input type="text" id="m-ch-pass"></div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Create', cls: 'btn-primary', action: createChannel },
    ]);
}

async function createChannel() {
    const body = {
        name: document.getElementById('m-ch-name').value,
        description: document.getElementById('m-ch-desc').value || null,
        max_users: parseInt(document.getElementById('m-ch-max').value) || null,
        position: parseInt(document.getElementById('m-ch-pos').value) || 0,
        password: document.getElementById('m-ch-pass').value || null,
    };
    const res = await api('POST', '/admin/channels', body);
    if (res) { closeModal(); loadChannels(); }
}

async function showEditChannel(channelId) {
    const data = await api('GET', '/admin/channels');
    if (!data) return;
    const c = data.channels.find(ch => ch.id === channelId);
    if (!c) return;
    openModal('Edit Channel: ' + c.name, `
        <div class="form-group"><label>Name</label><input type="text" id="m-ch-name" value="${esc(c.name)}"></div>
        <div class="form-group"><label>Description</label><input type="text" id="m-ch-desc" value="${esc(c.description || '')}"></div>
        <div class="form-group"><label>Max Users (0 = unlimited)</label><input type="number" id="m-ch-max" value="${c.max_users || 0}" min="0"></div>
        <div class="form-group"><label>Position</label><input type="number" id="m-ch-pos" value="${c.position || 0}" min="0"></div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Save', cls: 'btn-primary', action: () => updateChannel(channelId) },
    ]);
}

async function updateChannel(channelId) {
    const body = {
        name: document.getElementById('m-ch-name').value,
        description: document.getElementById('m-ch-desc').value || null,
        max_users: parseInt(document.getElementById('m-ch-max').value) || null,
        position: parseInt(document.getElementById('m-ch-pos').value) || 0,
    };
    const res = await api('PUT', '/admin/channels/' + channelId, body);
    if (res) { closeModal(); loadChannels(); }
}

async function deleteChannel(channelId, name) {
    if (!confirm('Delete channel "' + name + '"? Users in this channel will be disconnected.')) return;
    await api('DELETE', '/admin/channels/' + channelId);
    loadChannels();
}

// --- Roles ---

const PERMISSIONS = [
    { bit: 1, name: 'Join Channels' },
    { bit: 2, name: 'Speak' },
    { bit: 4, name: 'Create Channels' },
    { bit: 8, name: 'Manage Channels' },
    { bit: 16, name: 'Kick Users' },
    { bit: 32, name: 'Ban Users' },
    { bit: 64, name: 'Manage Roles' },
    { bit: 128, name: 'Admin' },
];

function permBits(val) {
    return PERMISSIONS.filter(p => val & p.bit).map(p => p.name).join(', ') || 'None';
}

function permCheckboxes(prefix, current) {
    return PERMISSIONS.map(p => `
        <div class="checkbox-row">
            <input type="checkbox" id="${prefix}-perm-${p.bit}" ${current & p.bit ? 'checked' : ''}>
            <label for="${prefix}-perm-${p.bit}">${p.name}</label>
        </div>
    `).join('');
}

function readPermCheckboxes(prefix) {
    let val = 0;
    PERMISSIONS.forEach(p => {
        if (document.getElementById(prefix + '-perm-' + p.bit).checked) val |= p.bit;
    });
    return val;
}

async function loadRoles() {
    const data = await api('GET', '/admin/roles');
    if (!data) return;
    const tbody = document.querySelector('#roles-table tbody');
    tbody.innerHTML = data.roles.map(r => `
        <tr>
            <td>${r.id}</td>
            <td>${esc(r.name)}</td>
            <td>${r.color ? '<span class="color-swatch" style="background:' + esc(r.color) + '"></span>' + esc(r.color) : '--'}</td>
            <td>${r.priority || 0}</td>
            <td>${permBits(r.permissions)}</td>
            <td>
                <button class="btn-icon" onclick="showEditRole(${r.id})" title="Edit">&#9998;</button>
                <button class="btn-icon danger" onclick="deleteRole(${r.id}, '${esc(r.name)}')" title="Delete">&#10005;</button>
            </td>
        </tr>
    `).join('');
}

function showCreateRole() {
    openModal('Create Role', `
        <div class="form-group"><label>Name</label><input type="text" id="m-role-name" required></div>
        <div class="form-group"><label>Color (hex, e.g. #ff6600)</label><input type="text" id="m-role-color" placeholder="#ffffff"></div>
        <div class="form-group"><label>Priority</label><input type="number" id="m-role-priority" value="10" min="0"></div>
        <div class="form-group"><label>Permissions</label>${permCheckboxes('m', 0)}</div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Create', cls: 'btn-primary', action: createRole },
    ]);
}

async function createRole() {
    const body = {
        name: document.getElementById('m-role-name').value,
        color: document.getElementById('m-role-color').value || null,
        priority: parseInt(document.getElementById('m-role-priority').value) || 10,
        permissions: readPermCheckboxes('m'),
    };
    const res = await api('POST', '/admin/roles', body);
    if (res) { closeModal(); loadRoles(); }
}

async function showEditRole(roleId) {
    const data = await api('GET', '/admin/roles');
    if (!data) return;
    const r = data.roles.find(rl => rl.id === roleId);
    if (!r) return;
    openModal('Edit Role: ' + r.name, `
        <div class="form-group"><label>Name</label><input type="text" id="m-role-name" value="${esc(r.name)}"></div>
        <div class="form-group"><label>Color</label><input type="text" id="m-role-color" value="${esc(r.color || '')}"></div>
        <div class="form-group"><label>Priority</label><input type="number" id="m-role-priority" value="${r.priority || 0}" min="0"></div>
        <div class="form-group"><label>Permissions</label>${permCheckboxes('m', r.permissions)}</div>
    `, [
        { text: 'Cancel', cls: 'btn-secondary', action: closeModal },
        { text: 'Save', cls: 'btn-primary', action: () => updateRole(roleId) },
    ]);
}

async function updateRole(roleId) {
    const body = {
        name: document.getElementById('m-role-name').value,
        color: document.getElementById('m-role-color').value || null,
        priority: parseInt(document.getElementById('m-role-priority').value) || 0,
        permissions: readPermCheckboxes('m'),
    };
    const res = await api('PUT', '/admin/roles/' + roleId, body);
    if (res) { closeModal(); loadRoles(); }
}

async function deleteRole(roleId, name) {
    if (!confirm('Delete role "' + name + '"?')) return;
    await api('DELETE', '/admin/roles/' + roleId);
    loadRoles();
}

// --- Audit ---

async function loadAudit() {
    const data = await api('GET', '/admin/audit?limit=100');
    if (!data) return;
    const tbody = document.querySelector('#audit-table tbody');
    tbody.innerHTML = data.entries.map(e => `
        <tr>
            <td>${new Date(e.created_at).toLocaleString()}</td>
            <td>${e.user_id || '--'}</td>
            <td>${esc(e.action)}</td>
            <td>${esc(e.resource_type || '')} ${e.resource_id ? '#' + e.resource_id : ''}</td>
            <td style="max-width:300px;overflow:hidden;text-overflow:ellipsis">${esc(e.details || '')}</td>
        </tr>
    `).join('');
}

// --- Modal ---

function openModal(title, bodyHtml, buttons) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-body').innerHTML = bodyHtml;
    const footer = document.getElementById('modal-footer');
    footer.innerHTML = '';
    buttons.forEach(b => {
        const btn = document.createElement('button');
        btn.className = 'btn ' + b.cls;
        btn.textContent = b.text;
        btn.onclick = b.action;
        footer.appendChild(btn);
    });
    document.getElementById('modal-overlay').style.display = 'flex';
}

function closeModal() {
    document.getElementById('modal-overlay').style.display = 'none';
}

// Close modal on overlay click
document.getElementById('modal-overlay').addEventListener('click', (e) => {
    if (e.target === e.currentTarget) closeModal();
});

// --- Util ---

function esc(str) {
    if (!str) return '';
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}
