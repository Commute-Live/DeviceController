const statusDot = document.getElementById('statusDot');
const statusText = document.getElementById('statusText');
const ssidSelect = document.getElementById('ssidSelect');
const scanBtn = document.getElementById('scanBtn');
const scanMeta = document.getElementById('scanMeta');
const connectBtn = document.getElementById('connectBtn');
const connectMeta = document.getElementById('connectMeta');
const errorBox = document.getElementById('errorBox');

function setStatus(connected, connecting, text) {
  statusDot.style.background = connected ? '#4c7c59' : connecting ? '#d9984a' : '#c5c5c5';
  statusText.textContent = text;
}

function showError(msg) {
  if (!msg) {
    errorBox.hidden = true;
    errorBox.textContent = '';
    return;
  }
  errorBox.hidden = false;
  errorBox.textContent = msg;
}

async function refreshStatus() {
  const res = await fetch('/api/status');
  const data = await res.json();

  if (data.connected) {
    window.location.href = '/hello.html';
    return;
  }

  if (data.connecting) {
    setStatus(false, true, 'Connecting…');
  } else {
    setStatus(false, false, 'Not connected');
  }

  showError(data.error || '');
}

async function scan() {
  scanMeta.textContent = 'Scanning…';
  const res = await fetch('/api/scan');
  const list = await res.json();

  ssidSelect.innerHTML = '';
  list.sort((a, b) => b.rssi - a.rssi);
  list.forEach(net => {
    const opt = document.createElement('option');
    opt.value = net.ssid;
    opt.textContent = `${net.ssid} (${net.secure ? 'Secure' : 'Open'}, ${net.rssi} dBm)`;
    ssidSelect.appendChild(opt);
  });

  scanMeta.textContent = `${list.length} networks`;
}

async function connect() {
  showError('');
  connectMeta.textContent = 'Sending…';

  const body = new URLSearchParams();
  body.set('ssid', ssidSelect.value);
  body.set('pass', document.getElementById('pass').value);
  body.set('user', document.getElementById('user').value);

  const res = await fetch('/api/connect', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  });

  const data = await res.json();
  if (!data.ok) {
    showError(data.error || 'Failed to start connection');
    connectMeta.textContent = '';
    return;
  }

  connectMeta.textContent = 'Connecting…';
}

scanBtn.addEventListener('click', scan);
connectBtn.addEventListener('click', connect);

scan();
setInterval(refreshStatus, 1500);
refreshStatus();
