// ============================================================
// VANITY — PebbleKit JS
// Fetches heart count + rank from rePebble App Store API
// ============================================================

var STORAGE_APP_ID = 'appStoreId';
var STORAGE_COLOR = 'heartColor';

function getAppStoreId() {
  return localStorage.getItem(STORAGE_APP_ID) || '18956d5f-1870-4716-b13d-1558cdf26f7a';
}
function saveAppStoreId(id) {
  localStorage.setItem(STORAGE_APP_ID, id);
}
function getHeartColor() {
  var c = localStorage.getItem(STORAGE_COLOR);
  return c !== null ? parseInt(c, 10) : 0;
}
function saveHeartColor(idx) {
  localStorage.setItem(STORAGE_COLOR, '' + idx);
}

// ============================================================
// Fetch hearts count
// ============================================================
function fetchHearts() {
  var appId = getAppStoreId();
  if (!appId || appId.length === 0) {
    console.log('Vanity: No app store ID configured');
    Pebble.sendAppMessage({ 'HEARTS': -1 });
    return;
  }

  var url = 'https://appstore-api.repebble.com/api/v1/apps/id/' + appId;
  console.log('Vanity: Fetching ' + url);

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    try {
      var json = JSON.parse(this.responseText);
      console.log('Vanity: Raw response: ' + this.responseText.substring(0, 200));
      // API may return data as array or direct object
      var entry = json.data;
      if (entry && typeof entry.length === 'number' && entry.length > 0) {
        entry = entry[0];
      }
      if (entry && typeof entry === 'object') {
        var hearts = entry.hearts || entry.hearts_count || entry.heart_count || 0;
        console.log('Vanity: Got ' + hearts + ' hearts');
        Pebble.sendAppMessage({ 'HEARTS': hearts }, function() {
          console.log('Vanity: Sent hearts');
        }, function(e) {
          console.log('Vanity: Send failed: ' + JSON.stringify(e));
        });

        // Also fetch rank
        fetchRank(appId, hearts);
      } else {
        console.log('Vanity: App not found in response');
        Pebble.sendAppMessage({ 'HEARTS': -1 });
      }
    } catch (e) {
      console.log('Vanity: Parse error: ' + e.message);
    }
  };
  xhr.onerror = function() {
    console.log('Vanity: Network error');
  };
  xhr.open('GET', url);
  xhr.send();
}

// ============================================================
// Fetch rank among all watchfaces sorted by hearts
// Walk pages until we find our app or pass our heart count
// ============================================================
function fetchRank(appId, myHearts) {
  var rank = 0;
  var offset = 0;
  var limit = 100;
  var found = false;
  var maxPages = 50; // safety limit: 5000 apps max
  var page = 0;

  function fetchPage() {
    if (page >= maxPages) {
      console.log('Vanity: Rank search exceeded max pages');
      return;
    }

    var url = 'https://appstore-api.repebble.com/api/v1/apps/collection/all/watchfaces' +
              '?sort=hearts&limit=' + limit + '&offset=' + offset;

    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
      try {
        var json = JSON.parse(this.responseText);
        if (!json.data || json.data.length === 0) {
          // Ran out of apps without finding ours
          console.log('Vanity: App not found in rankings');
          return;
        }

        for (var i = 0; i < json.data.length; i++) {
          rank++;
          if (json.data[i].id === appId) {
            found = true;
            console.log('Vanity: Rank is #' + rank);
            Pebble.sendAppMessage({ 'RANK': rank }, function() {
              console.log('Vanity: Sent rank');
            }, function(e) {
              console.log('Vanity: Rank send failed');
            });
            return;
          }

          // Optimization: if this app's hearts are less than ours,
          // something's wrong (list is sorted desc), keep going
          // If hearts drop well below ours and we haven't found it,
          // the app might be unlisted
          if (json.data[i].hearts < myHearts - 1 && rank > 10) {
            // We should have found it by now if sorted correctly
            // Keep going a bit more but don't go forever
          }
        }

        // Next page
        offset += limit;
        page++;
        fetchPage();

      } catch (e) {
        console.log('Vanity: Rank parse error: ' + e.message);
      }
    };
    xhr.onerror = function() {
      console.log('Vanity: Rank network error');
    };
    xhr.open('GET', url);
    xhr.send();
  }

  fetchPage();
}

// ============================================================
// Events
// ============================================================
Pebble.addEventListener('ready', function() {
  console.log('Vanity: JS ready');
  Pebble.sendAppMessage({ 'HEART_COLOR': getHeartColor() });
  fetchHearts();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload['REQUEST_HEARTS']) {
    fetchHearts();
  }
});

// ============================================================
// Configuration
// ============================================================
Pebble.addEventListener('showConfiguration', function() {
  var currentId = getAppStoreId();
  var currentColor = getHeartColor();
  var html = generateConfigPage(currentId, currentColor);
  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response && e.response.length > 0) {
    try {
      var config = JSON.parse(decodeURIComponent(e.response));
      console.log('Vanity: Config: ' + JSON.stringify(config));

      if (config.appStoreId !== undefined) {
        saveAppStoreId(config.appStoreId);
      }
      if (config.heartColor !== undefined) {
        var colorIdx = parseInt(config.heartColor, 10);
        saveHeartColor(colorIdx);
        Pebble.sendAppMessage({ 'HEART_COLOR': colorIdx });
      }

      fetchHearts();
    } catch (err) {
      console.log('Vanity: Config parse error: ' + err.message);
    }
  }
});

// ============================================================
// Config page
// ============================================================
function generateConfigPage(currentId, currentColor) {
  var colors = [
    { name: 'Red', hex: '#e74c3c', idx: 0 },
    { name: 'Blue', hex: '#4a90d9', idx: 1 },
    { name: 'Green', hex: '#2ecc71', idx: 2 },
    { name: 'Purple', hex: '#9b59b6', idx: 3 },
    { name: 'Orange', hex: '#e67e22', idx: 4 },
    { name: 'Pink', hex: '#e91e90', idx: 5 }
  ];

  var colorButtons = '';
  for (var i = 0; i < colors.length; i++) {
    var c = colors[i];
    var selected = (c.idx === currentColor) ? ' selected' : '';
    colorButtons += '<button class="color-btn' + selected + '" ' +
      'data-idx="' + c.idx + '" ' +
      'style="background:' + c.hex + ';" ' +
      'onclick="selectColor(' + c.idx + ')" ' +
      'title="' + c.name + '">' +
      (c.idx === currentColor ? '\u2714' : '') +
      '</button>';
  }

  return '<!DOCTYPE html>' +
    '<html><head>' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>Vanity Settings</title>' +
    '<style>' +
    '* { box-sizing: border-box; }' +
    'body { font-family: -apple-system, "Segoe UI", sans-serif; background: #0a0a1a; color: #eee; margin: 0; padding: 20px; }' +
    '.container { max-width: 400px; margin: 0 auto; }' +
    'h1 { text-align: center; color: #e74c3c; font-size: 28px; margin: 0 0 4px 0; letter-spacing: 2px; }' +
    '.subtitle { text-align: center; color: #666; font-size: 13px; margin-bottom: 24px; font-style: italic; }' +
    '.heart-icon { text-align: center; font-size: 48px; margin: 16px 0 8px 0; }' +
    '.section { background: #111128; border: 1px solid #222; border-radius: 12px; padding: 20px; margin-bottom: 16px; }' +
    '.section-title { font-size: 11px; color: #666; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 12px; font-weight: 700; }' +
    'input[type=text] { width: 100%; padding: 14px; font-size: 15px; border: 2px solid #222; border-radius: 8px; background: #0a0a1a; color: #fff; font-family: "SF Mono", "Courier New", monospace; }' +
    'input[type=text]:focus { border-color: #e74c3c; outline: none; }' +
    '.help { font-size: 12px; color: #555; margin-top: 8px; line-height: 1.6; }' +
    '.help code { background: #1a1a2e; padding: 2px 6px; border-radius: 3px; color: #e74c3c; font-size: 11px; }' +
    '.color-grid { display: flex; gap: 10px; flex-wrap: wrap; justify-content: center; }' +
    '.color-btn { width: 48px; height: 48px; border-radius: 50%; border: 3px solid transparent; cursor: pointer; font-size: 18px; color: #fff; transition: transform 0.15s, border-color 0.15s; }' +
    '.color-btn:active { transform: scale(0.9); }' +
    '.color-btn.selected { border-color: #fff; transform: scale(1.1); box-shadow: 0 0 12px rgba(255,255,255,0.3); }' +
    'button.action { width: 100%; padding: 16px; font-size: 17px; font-weight: 700; border: none; border-radius: 10px; cursor: pointer; margin-bottom: 10px; letter-spacing: 0.5px; }' +
    '.save-btn { background: #e74c3c; color: #fff; }' +
    '.save-btn:active { background: #c0392b; }' +
    '.cancel-btn { background: #1a1a2e; color: #666; border: 1px solid #333 !important; }' +
    '.footer { text-align: center; color: #333; font-size: 11px; margin-top: 20px; }' +
    '</style></head><body>' +
    '<div class="container">' +
    '<div class="heart-icon">\u2764\uFE0F</div>' +
    '<h1>VANITY</h1>' +
    '<p class="subtitle">It\'s watching its own popularity</p>' +

    '<div class="section">' +
    '<div class="section-title">App Store ID</div>' +
    '<input type="text" id="appStoreId" placeholder="Paste your app store ID here" value="' + (currentId || '') + '">' +
    '<p class="help">After publishing, find your ID in the store URL:<br>' +
    '<code>apps.rebble.io/.../<b>YOUR_ID</b></code></p>' +
    '</div>' +

    '<div class="section">' +
    '<div class="section-title">Heart Color</div>' +
    '<div class="color-grid">' + colorButtons + '</div>' +
    '</div>' +

    '<button class="action save-btn" onclick="save()">Save</button>' +
    '<button class="action cancel-btn" onclick="cancel()">Cancel</button>' +

    '<p class="footer">Vanity v1.0 \u2022 A watchface that can\'t stop checking</p>' +
    '</div>' +

    '<script>' +
    'var selectedColor = ' + currentColor + ';' +
    'function selectColor(idx) {' +
    '  selectedColor = idx;' +
    '  var btns = document.querySelectorAll(".color-btn");' +
    '  for (var i = 0; i < btns.length; i++) {' +
    '    btns[i].classList.remove("selected");' +
    '    btns[i].textContent = "";' +
    '  }' +
    '  var sel = document.querySelector("[data-idx=\\"" + idx + "\\"]");' +
    '  if (sel) { sel.classList.add("selected"); sel.textContent = "\\u2714"; }' +
    '}' +
    'function save() {' +
    '  var config = {' +
    '    appStoreId: document.getElementById("appStoreId").value.trim(),' +
    '    heartColor: selectedColor' +
    '  };' +
    '  window.location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(config));' +
    '}' +
    'function cancel() {' +
    '  window.location.href = "pebblejs://close";' +
    '}' +
    '</script></body></html>';
}
