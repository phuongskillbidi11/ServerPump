// ============================================
// CONSTANTS
// ============================================
const API = 'http://localhost:8080';
const STATUS_TEXT = ['Unknown', 'Running', 'Stopped', 'Error'];
const STATUS_ICONS = ['help', 'play_circle', 'stop_circle', 'error'];
const BUSY_TEXT = ['Idle', 'Starting P1', 'Starting P2'];
const BUSY_ICONS = ['check_circle', 'sync', 'sync'];

// ============================================
// HISTORY STATE
// ============================================
let historyCache = null;
let filteredData = [];
let currentPage = 1;
let rowsPerPage = 20;
let sortColumn = 'timestamp';
let sortDirection = 'desc';
let activeFilters = {
    pump1Status: 'all',
    pump2Status: 'all',
    searchTerm: '',
    dateFrom: null,
    dateTo: null
};

// ============================================
// SIDEBAR & NAVIGATION
// ============================================
function toggleSidebar() {
    document.getElementById('sidebar').classList.toggle('collapsed');
}

function showPage(page) {
    // Hide all pages
    document.querySelectorAll('[id^="page-"]').forEach(el => el.classList.add('hidden'));
    
    // Show selected page
    document.getElementById('page-' + page).classList.remove('hidden');
    
    // Update active nav item
    document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
    event.currentTarget.classList.add('active');
    
    // Load history when switching to history page
    if (page === 'history') {
        loadHistory();
    }
    
    // Close mobile menu
    if (window.innerWidth <= 1024) {
        closeMobileMenu();
    }
}

// Mobile menu handling
function toggleMobileMenu() {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('overlay');
    const btn = document.getElementById('mobileMenuBtn');
    
    sidebar.classList.toggle('mobile-open');
    overlay.classList.toggle('active');
    
    if (sidebar.classList.contains('mobile-open')) {
        btn.querySelector('span').textContent = 'close';
    } else {
        btn.querySelector('span').textContent = 'menu';
    }
}

function closeMobileMenu() {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('overlay');
    const btn = document.getElementById('mobileMenuBtn');
    
    sidebar.classList.remove('mobile-open');
    overlay.classList.remove('active');
    btn.querySelector('span').textContent = 'menu';
}

window.addEventListener('resize', () => {
    if (window.innerWidth > 1024) {
        closeMobileMenu();
    }
});

// ============================================
// DASHBOARD FUNCTIONS
// ============================================
async function loadStatus() {
    try {
        const res = await fetch(`${API}/api/pump/status`);
        const data = await res.json();
        
        document.getElementById('busyStatus').innerHTML = `
            <span class="material-symbols-rounded">${BUSY_ICONS[data.busy]}</span>
            ${BUSY_TEXT[data.busy] || '--'}
        `;
        document.getElementById('alarmStatus').innerHTML = data.alarm ? 
            '<span class="material-symbols-rounded" style="color: #dc3545;">warning</span> ACTIVE' : 
            '<span class="material-symbols-rounded" style="color: #28a745;">check_circle</span> OK';
        
        const grid = document.getElementById('pumpGrid');
        grid.innerHTML = `
            ${createPumpCard(1, data.pump1, data.pump1_status)}
            ${createPumpCard(2, data.pump2, data.pump2_status)}
        `;
        
        document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
    } catch (err) {
        console.error('Error:', err);
    }
}

function createPumpCard(id, cmd, status) {
    const statusClass = ['unknown', 'running', 'stopped', 'error'][status] || 'unknown';
    const statusIcon = STATUS_ICONS[status] || 'help';
    
    return `
        <div class="pump-card ${statusClass}">
            <div class="pump-card-header">
                <div class="pump-title">
                    <span class="material-symbols-rounded icon-lg">water_drop</span>
                    Pump ${id}
                </div>
                <span class="status-badge ${statusClass}">
                    <span class="material-symbols-rounded" style="font-size: 16px;">${statusIcon}</span>
                    ${STATUS_TEXT[status]}
                </span>
            </div>
            <div class="pump-info">
                <div class="info-row">
                    <span class="info-label">
                        <span class="material-symbols-rounded">radio_button_checked</span>
                        Command:
                    </span>
                    <span class="info-value">
                        <span class="material-symbols-rounded">${cmd ? 'toggle_on' : 'toggle_off'}</span>
                        ${cmd ? 'ON' : 'OFF'}
                    </span>
                </div>
                <div class="info-row">
                    <span class="info-label">
                        <span class="material-symbols-rounded">electric_bolt</span>
                        Hardware Status:
                    </span>
                    <span class="info-value">
                        <span class="material-symbols-rounded">${statusIcon}</span>
                        ${STATUS_TEXT[status]}
                    </span>
                </div>
            </div>
            <div class="controls">
                <button class="btn btn-on" onclick="control(${id}, 1)">
                    <span class="material-symbols-rounded">power_settings_new</span>
                    TURN ON
                </button>
                <button class="btn btn-off" onclick="control(${id}, 0)">
                    <span class="material-symbols-rounded">power_off</span>
                    TURN OFF
                </button>
            </div>
        </div>
    `;
}

async function control(pumpId, state) {
    try {
        await fetch(`${API}/api/pump/control`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({pump_id: pumpId, state: state})
        });
        setTimeout(loadStatus, 200);
    } catch (err) {
        alert('Error: ' + err.message);
    }
}

async function loadGateway() {
    try {
        const res = await fetch(`${API}/api/gateway/status`);
        const data = await res.json();
        
        const dot = document.getElementById('gatewayDot');
        dot.className = 'status-dot ' + (data.status ? 'online' : 'offline');
        
        document.getElementById('deviceId').textContent = data.device_id || 'N/A';
        document.getElementById('firmware').textContent = data.firmware || 'N/A';
        document.getElementById('lastSeen').textContent = data.last_seen ? 
            new Date(data.last_seen * 1000).toLocaleString() : 'Never';
    } catch (err) {
        console.error('Gateway error:', err);
    }
}

// ============================================
// HISTORY FUNCTIONS - MAIN LOAD
// ============================================
async function loadHistory() {
    showHistoryLoading();
    
    try {
        // Build API URL with date range
        let apiUrl = `${API}/api/pump/history?limit=5000`;
        
        if (activeFilters.dateFrom) {
            const fromTimestamp = Math.floor(activeFilters.dateFrom.getTime() / 1000);
            apiUrl += `&from=${fromTimestamp}`;
            console.log('[LOAD] From:', activeFilters.dateFrom, '→ timestamp:', fromTimestamp);
        }
        
        if (activeFilters.dateTo) {
            const toTimestamp = Math.floor(activeFilters.dateTo.getTime() / 1000);
            apiUrl += `&to=${toTimestamp}`;
            console.log('[LOAD] To:', activeFilters.dateTo, '→ timestamp:', toTimestamp);
        }
        
        console.log('[LOAD] Fetching:', apiUrl);
        
        const res = await fetch(apiUrl);
        const data = await res.json();
        
        console.log('[LOAD] Received:', data.count, 'records');
        
        if (data.count > 0) {
            const firstRecord = new Date(data.data[0].timestamp * 1000);
            const lastRecord = new Date(data.data[data.count - 1].timestamp * 1000);
            console.log('[LOAD] Data range:', lastRecord, '→', firstRecord);
        }
        
        historyCache = data;
        filteredData = [...data.data];
        currentPage = 1;
        
        applyLocalFilters();
        
    } catch (err) {
        showHistoryError(err);
    }
}
function showHistoryLoading() {
    const tbody = document.getElementById('historyBody');
    tbody.innerHTML = `
        <tr>
            <td colspan="7">
                <div class="loading-container">
                    <div class="loading-spinner"></div>
                    <div class="loading-text">Loading history data...</div>
                </div>
            </td>
        </tr>
    `;
    document.getElementById('paginationContainer').innerHTML = '';
}

function showHistoryError(err) {
    const tbody = document.getElementById('historyBody');
    tbody.innerHTML = `
        <tr>
            <td colspan="7">
                <div class="error-container">
                    <span class="material-symbols-rounded">error</span>
                    <div class="error-message">Failed to load history data</div>
                    <div style="color: #999; margin-bottom: 20px;">${err.message}</div>
                    <button onclick="refreshHistory()" class="btn-action btn-refresh">
                        <span class="material-symbols-rounded">refresh</span>
                        Try Again
                    </button>
                </div>
            </td>
        </tr>
    `;
    document.getElementById('paginationContainer').innerHTML = '';
}

function refreshHistory() {
    historyCache = null;
    filteredData = [];
    currentPage = 1;
    activeFilters = {
        pump1Status: 'all',
        pump2Status: 'all',
        searchTerm: '',
        dateFrom: null,
        dateTo: null
    };
    
    // Reset filters UI
    const filterP1 = document.getElementById('filterP1');
    const filterP2 = document.getElementById('filterP2');
    const searchInput = document.getElementById('searchInput');
    const datetimeFrom = document.getElementById('datetimeFrom');
    const datetimeTo = document.getElementById('datetimeTo');
    
    if (filterP1) filterP1.value = 'all';
    if (filterP2) filterP2.value = 'all';
    if (searchInput) searchInput.value = '';
    if (datetimeFrom) datetimeFrom.value = '';
    if (datetimeTo) datetimeTo.value = '';
    
    // Activate "All" quick filter
    document.querySelectorAll('.quick-filter-btn').forEach((btn, idx) => {
        if (idx === 0) btn.classList.add('active');
        else btn.classList.remove('active');
    });
    
    loadHistory();
}

// ============================================
// FILTERING & SEARCHING
// ============================================
function applyLocalFilters() {
    if (!historyCache) return;
    
    let data = [...historyCache.data];
    
    // Search filter
    if (activeFilters.searchTerm) {
        const term = activeFilters.searchTerm.toLowerCase();
        data = data.filter(item => {
            const timestamp = new Date(item.timestamp * 1000).toLocaleString().toLowerCase();
            const p1Status = STATUS_TEXT[item.pump1_status || 0].toLowerCase();
            const p2Status = STATUS_TEXT[item.pump2_status || 0].toLowerCase();
            return timestamp.includes(term) || p1Status.includes(term) || p2Status.includes(term);
        });
    }
    
    // Status filters
    if (activeFilters.pump1Status !== 'all') {
        const statusNum = parseInt(activeFilters.pump1Status);
        data = data.filter(item => (item.pump1_status || 0) === statusNum);
    }
    
    if (activeFilters.pump2Status !== 'all') {
        const statusNum = parseInt(activeFilters.pump2Status);
        data = data.filter(item => (item.pump2_status || 0) === statusNum);
    }
    
    // Sorting
    data.sort((a, b) => {
        let aVal = a[sortColumn] || 0;
        let bVal = b[sortColumn] || 0;
        return sortDirection === 'asc' ? (aVal > bVal ? 1 : -1) : (aVal < bVal ? 1 : -1);
    });
    
    filteredData = data;
    
    const totalPages = Math.ceil(filteredData.length / rowsPerPage);
    if (currentPage > totalPages && totalPages > 0) currentPage = 1;
    
    renderHistoryTable();
    renderPagination();
    updateHistoryStats();
    updateSortIndicators();
}

// Debounce function
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func(...args), wait);
    };
}

// Search handler with debounce
const handleSearchInput = debounce((value) => {
    activeFilters.searchTerm = value;
    currentPage = 1;
    applyLocalFilters();  // LOCAL, không reload BE
}, 300);

function filterByStatus(pump, status) {
    if (pump === 'pump1') activeFilters.pump1Status = status;
    else if (pump === 'pump2') activeFilters.pump2Status = status;
    currentPage = 1;
    applyLocalFilters();  // LOCAL, không reload BE
}


// ============================================
// DATE FILTERING
// ============================================
function applyQuickFilter(range) {
    document.querySelectorAll('.quick-filter-btn').forEach(btn => btn.classList.remove('active'));
    event.currentTarget.classList.add('active');
    
    const now = new Date();
    let fromDate = null;
    let toDate = null;
    
    switch(range) {
        case 'all':
            fromDate = null;
            toDate = null;
            break;
            
        case 'today':
            fromDate = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0);
            toDate = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59);
            break;
            
        case 'yesterday':
            fromDate = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1, 0, 0, 0);
            toDate = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 1, 23, 59, 59);
            break;
            
        case 'week':
            fromDate = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 7, 0, 0, 0);
            toDate = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59);
            break;
            
        case 'month':
            fromDate = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 30, 0, 0, 0);
            toDate = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59);
            break;
    }
    
    activeFilters.dateFrom = fromDate;
    activeFilters.dateTo = toDate;
    
    // Update UI inputs
    const datetimeFrom = document.getElementById('datetimeFrom');
    const datetimeTo = document.getElementById('datetimeTo');
    
    if (datetimeFrom) {
        datetimeFrom.value = fromDate ? toISOLocal(fromDate) : '';
    }
    
    if (datetimeTo) {
        datetimeTo.value = toDate ? toISOLocal(toDate) : '';
    }
    
    console.log('[FILTER] Date range:', fromDate, '→', toDate);
    
    // RELOAD FROM BE
    loadHistory();
}

function filterByDateRange() {
    const fromInput = document.getElementById('datetimeFrom').value;
    const toInput = document.getElementById('datetimeTo').value;
    
    activeFilters.dateFrom = fromInput ? new Date(fromInput) : null;
    activeFilters.dateTo = toInput ? new Date(toInput) : null;
    
    document.querySelectorAll('.quick-filter-btn').forEach(btn => btn.classList.remove('active'));
    
    // RELOAD FROM BE
    loadHistory();
}

function clearDateFilter() {
    document.getElementById('datetimeFrom').value = '';
    document.getElementById('datetimeTo').value = '';
    activeFilters.dateFrom = null;
    activeFilters.dateTo = null;
    
    document.querySelectorAll('.quick-filter-btn').forEach((btn, idx) => {
        if (idx === 0) btn.classList.add('active');
        else btn.classList.remove('active');
    });
    
    loadHistory();
}
// ============================================
// SORTING
// ============================================
function sortTable(column) {
    if (sortColumn === column) {
        // Toggle direction
        sortDirection = sortDirection === 'asc' ? 'desc' : 'asc';
    } else {
        sortColumn = column;
        sortDirection = 'desc'; // Default to descending
    }
    
    applyLocalFilters();  // LOCAL, không reload BE
}

function updateSortIndicators() {
    // Remove all sort classes
    document.querySelectorAll('th.sortable').forEach(th => {
        th.classList.remove('sort-asc', 'sort-desc');
    });
    
    // Add class to current sort column
    const th = document.querySelector(`th[data-column="${sortColumn}"]`);
    if (th) {
        th.classList.add(`sort-${sortDirection}`);
    }
}

// ============================================
// TABLE RENDERING
// ============================================
function renderHistoryTable() {
    const tbody = document.getElementById('historyBody');
    
    // Check if there's data
    if (filteredData.length === 0) {
        tbody.innerHTML = `
            <tr>
                <td colspan="7">
                    <div class="empty-state">
                        <span class="material-symbols-rounded">inbox</span>
                        <div class="empty-message">No records found</div>
                    </div>
                </td>
            </tr>
        `;
        return;
    }
    
    // Calculate pagination
    const start = (currentPage - 1) * rowsPerPage;
    const end = start + rowsPerPage;
    const pageData = filteredData.slice(start, end);
    
    // Render rows
    tbody.innerHTML = pageData.map(item => {
        const p1Status = item.pump1_status !== undefined ? item.pump1_status : 0;
        const p2Status = item.pump2_status !== undefined ? item.pump2_status : 0;
        
        return `
            <tr>
                <td>
                    <div class="status-cell">
                        <span class="material-symbols-rounded">event</span>
                        ${new Date(item.timestamp * 1000).toLocaleString()}
                    </div>
                </td>
                <td>
                    <span class="badge ${item.pump1 ? 'badge-success' : 'badge-secondary'}">
                        <span class="material-symbols-rounded">${item.pump1 ? 'toggle_on' : 'toggle_off'}</span>
                        ${item.pump1 ? 'ON' : 'OFF'}
                    </span>
                </td>
                <td>
                    <span class="badge ${getBadgeClass(p1Status)}">
                        <span class="material-symbols-rounded">${STATUS_ICONS[p1Status]}</span>
                        ${STATUS_TEXT[p1Status]}
                    </span>
                </td>
                <td>
                    <span class="badge ${item.pump2 ? 'badge-success' : 'badge-secondary'}">
                        <span class="material-symbols-rounded">${item.pump2 ? 'toggle_on' : 'toggle_off'}</span>
                        ${item.pump2 ? 'ON' : 'OFF'}
                    </span>
                </td>
                <td>
                    <span class="badge ${getBadgeClass(p2Status)}">
                        <span class="material-symbols-rounded">${STATUS_ICONS[p2Status]}</span>
                        ${STATUS_TEXT[p2Status]}
                    </span>
                </td>
                <td>
                    <span class="badge ${(item.busy || 0) > 0 ? 'badge-warning' : 'badge-success'}">
                        <span class="material-symbols-rounded">${BUSY_ICONS[item.busy || 0]}</span>
                        ${BUSY_TEXT[item.busy || 0]}
                    </span>
                </td>
                <td>
                    <span class="badge ${(item.alarm || 0) ? 'badge-danger' : 'badge-success'}">
                        <span class="material-symbols-rounded">${(item.alarm || 0) ? 'warning' : 'check_circle'}</span>
                        ${(item.alarm || 0) ? 'ACTIVE' : 'OK'}
                    </span>
                </td>
            </tr>
        `;
    }).join('');
}

function getBadgeClass(status) {
    const classes = ['badge-warning', 'badge-success', 'badge-secondary', 'badge-danger'];
    return classes[status] || 'badge-secondary';
}

// ============================================
// PAGINATION
// ============================================
function renderPagination() {
    const totalPages = Math.ceil(filteredData.length / rowsPerPage);
    
    if (totalPages === 0) {
        document.getElementById('paginationContainer').innerHTML = '';
        return;
    }
    
    const container = document.getElementById('paginationContainer');
    container.innerHTML = `
        <div class="pagination">
            <button onclick="goToPage(1)" ${currentPage === 1 ? 'disabled' : ''}>
                <span class="material-symbols-rounded">first_page</span>
            </button>
            <button onclick="goToPage(${currentPage - 1})" ${currentPage === 1 ? 'disabled' : ''}>
                <span class="material-symbols-rounded">chevron_left</span>
            </button>
            
            <span class="page-info">
                Page ${currentPage} of ${totalPages}
            </span>
            
            <button onclick="goToPage(${currentPage + 1})" ${currentPage === totalPages ? 'disabled' : ''}>
                <span class="material-symbols-rounded">chevron_right</span>
            </button>
            <button onclick="goToPage(${totalPages})" ${currentPage === totalPages ? 'disabled' : ''}>
                <span class="material-symbols-rounded">last_page</span>
            </button>
        </div>
        
        <div class="rows-per-page">
            <label>Rows per page:</label>
            <select onchange="changeRowsPerPage(this.value)">
                <option value="10" ${rowsPerPage === 10 ? 'selected' : ''}>10</option>
                <option value="20" ${rowsPerPage === 20 ? 'selected' : ''}>20</option>
                <option value="50" ${rowsPerPage === 50 ? 'selected' : ''}>50</option>
                <option value="100" ${rowsPerPage === 100 ? 'selected' : ''}>100</option>
            </select>
        </div>
    `;
}

function goToPage(page) {
    const totalPages = Math.ceil(filteredData.length / rowsPerPage);
    if (page < 1 || page > totalPages) return;
    
    currentPage = page;
    renderHistoryTable();
    renderPagination();
}

function changeRowsPerPage(value) {
    rowsPerPage = parseInt(value);
    currentPage = 1; // Reset to page 1
    renderHistoryTable();
    renderPagination();
}

// ============================================
// STATS UPDATE
// ============================================
function updateHistoryStats() {
    if (!historyCache) return;
    
    // Total records
    document.getElementById('totalRecords').textContent = historyCache.count || historyCache.data.length;
    
    // Filtered records
    document.getElementById('filteredRecords').textContent = filteredData.length;
    
    // Date range - HIỂN THỊ THEO FILTER USER CHỌN (không tính từ data)
    const dateRangeEl = document.getElementById('dateRange');
    
    if (activeFilters.dateFrom || activeFilters.dateTo) {
        // User đã chọn date range → Hiển thị chính xác cái họ chọn
        const formatDate = (date) => {
            if (!date) return '--';
            return `${date.getMonth() + 1}/${date.getDate()}`;
        };
        
        const formatDateTime = (date) => {
            if (!date) return '--';
            const h = date.getHours();
            const m = date.getMinutes().toString().padStart(2, '0');
            return `${date.getMonth() + 1}/${date.getDate()} ${h}:${m}`;
        };
        
        const from = activeFilters.dateFrom ? formatDateTime(activeFilters.dateFrom) : '--';
        const to = activeFilters.dateTo ? formatDateTime(activeFilters.dateTo) : 'now';
        
        dateRangeEl.textContent = `${from} - ${to}`;
        
    } else if (filteredData.length > 0) {
        // Không có filter → Hiển thị range của data
        const timestamps = filteredData.map(item => item.timestamp);
        const minDate = new Date(Math.min(...timestamps) * 1000);
        const maxDate = new Date(Math.max(...timestamps) * 1000);
        
        const formatDate = (date) => {
            return `${date.getMonth() + 1}/${date.getDate()}`;
        };
        
        dateRangeEl.textContent = `${formatDate(minDate)} - ${formatDate(maxDate)}`;
        
    } else {
        dateRangeEl.textContent = '--';
    }
}
// ============================================
// EXPORT CSV
// ============================================
function exportToCSV() {
    if (!filteredData.length) {
        alert('No data to export');
        return;
    }
    
    // CSV Header
    const headers = ['Timestamp', 'P1 Command', 'P1 Status', 'P2 Command', 'P2 Status', 'Inverter', 'Alarm'];
    
    // CSV Rows
    const rows = filteredData.map(item => {
        const p1Status = item.pump1_status !== undefined ? item.pump1_status : 0;
        const p2Status = item.pump2_status !== undefined ? item.pump2_status : 0;
        const busy = item.busy !== undefined ? item.busy : 0;
        const alarm = item.alarm !== undefined ? item.alarm : 0;
        
        return [
            new Date(item.timestamp * 1000).toLocaleString(),
            item.pump1 ? 'ON' : 'OFF',
            STATUS_TEXT[p1Status],
            item.pump2 ? 'ON' : 'OFF',
            STATUS_TEXT[p2Status],
            BUSY_TEXT[busy],
            alarm ? 'ACTIVE' : 'OK'
        ];
    });
    
    // Build CSV
    let csv = headers.join(',') + '\n';
    rows.forEach(row => {
        csv += row.map(cell => `"${cell}"`).join(',') + '\n';
    });
    
    // Download
    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `pump_history_${Date.now()}.csv`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
}
// Helper: Convert Date to ISO local string
function toISOLocal(date) {
    const offset = date.getTimezoneOffset() * 60000;
    return new Date(date.getTime() - offset).toISOString();
}
// ============================================
// AUTO-REFRESH & INITIALIZATION
// ============================================
setInterval(() => {
    loadStatus();
    loadGateway();
}, 2000);

// Initial load
loadStatus();
loadGateway();