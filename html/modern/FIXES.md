# Fixes für das moderne UI

## Problem 1: URL-Routing

**Aktuell:** `http://server:8082/modern/index.html`  
**Gewünscht:** `http://server:8082/modern/`

### Lösung:
Das Backend liefert bereits automatisch `index.html` aus, wenn ein Ordner aufgerufen wird.
Die URL `http://server:8082/modern/` sollte funktionieren.

---

## Problem 2: Echte Track-Daten laden

### Aktueller Code (Zeile ~470):
```javascript
// Load tracks from backend
async function loadTracks() {
    // Demo tracks for now
    const tracks = [
        { name: 'Gleis 1', status: 'free', loco: '' },
        { name: 'Gleis 2', status: 'occupied', loco: 'BR 103' },
        { name: 'Gleis 3', status: 'reserved', loco: 'ICE 3' },
        { name: 'Gleis 4', status: 'occupied', loco: 'BESETZT' }
    ];
    
    const grid = document.getElementById('trackGrid');
    grid.innerHTML = tracks.map(track => `
        <div class="track ${track.status}">
            <div class="track-name">${track.name}</div>
            <div class="track-loco">${track.loco}</div>
        </div>
    `).join('');
}
```

### Neuer Code:
```javascript
// Configuration (ganz oben im <script> Tag, Zeile ~395)
const BACKEND_URL = window.location.origin;
const API_URL = window.location.protocol + '//' + window.location.hostname + ':8083';  // NEU

// Load tracks from backend API
async function loadTracks() {
    try {
        const response = await fetch(`${API_URL}/api/v1/layout?layer=1`);
        const data = await response.json();
        
        const tracks = data.items.filter(item => item.type === 'track');
        
        if (tracks.length === 0) {
            displayDemoTracks();
            return;
        }
        
        const grid = document.getElementById('trackGrid');
        grid.innerHTML = tracks.map(track => {
            let status = 'free';
            let locoName = '';
            
            if (track.blocked) {
                status = 'occupied';
                locoName = 'GESPERRT';
            } else if (track.occupied) {
                status = 'occupied';
                locoName = track.locoName || 'BESETZT';
            } else if (track.reserved) {
                status = 'reserved';
                locoName = track.locoName || 'RESERVIERT';
            }
            
            return `
                <div class="track ${status}">
                    <div class="track-name">${track.name}</div>
                    <div class="track-loco">${locoName}</div>
                </div>
            `;
        }).join('');
    } catch (error) {
        console.error('Error loading tracks:', error);
        displayDemoTracks();
    }
}

// Fallback für Demo-Daten (neu hinzufügen)
function displayDemoTracks() {
    const tracks = [
        { name: 'Gleis 1', status: 'free', loco: '' },
        { name: 'Gleis 2', status: 'occupied', loco: 'BR 103' },
        { name: 'Gleis 3', status: 'reserved', loco: 'ICE 3' },
        { name: 'Gleis 4', status: 'free', loco: '' }
    ];
    
    const grid = document.getElementById('trackGrid');
    grid.innerHTML = tracks.map(track => `
        <div class="track ${track.status}">
            <div class="track-name">${track.name}</div>
            <div class="track-loco">${track.loco}</div>
        </div>
    `).join('');
}
```

---

## Wichtig:

**API-Server muss aktiviert sein!**

In `railcontrol.conf`:
```
apiserver = true
apiserverport = 8083
```

Das hast du bereits korrekt konfiguriert ?
