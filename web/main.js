// SPDX-License-Identifier: BSD-3-Clause

import createPicoTrackerModule from './wasm/picotracker.js';
import {
  attachTouchControls,
  createInputManager,
  isMobileTouchBrowser,
} from './input.js';
import { storage } from './storage.js';

const statusText = document.getElementById('status-text');
const statusDot = document.getElementById('status-dot');
const storageStatus = document.getElementById('storage-status');
const canvas = document.getElementById('picotracker-canvas');
const menuToggle = document.getElementById('menu-toggle');
const menuPanel = document.getElementById('menu-panel');
const btnFiles = document.getElementById('btn-files');
const btnCredits = document.getElementById('btn-credits');
const creditsDialog = document.getElementById('credits-dialog');
const creditsContent = document.getElementById('credits-content');
const filesModal = document.getElementById('files-modal');
const modalCloseBtn = document.getElementById('modal-close-btn');
const btnModalDone = document.getElementById('btn-modal-done');
const btnLockRetry = document.getElementById('btn-lock-retry');
const mobileControls = document.getElementById('mobile-controls');

let currentModule = null;
let currentInputManager = null;
let detachTouchControls = null;
let audioNeedsGesture = true;

const mobileTouchEnabled = isMobileTouchBrowser();
document.body.classList.toggle('mobile-controls-enabled', mobileTouchEnabled);

function updateStatus(text, state = 'pending') {
  if (statusText) statusText.textContent = text;
  if (statusDot) {
    statusDot.className = 'status-dot' + (state === 'ready' ? ' ready' : state === 'error' ? ' error' : '');
  }
  console.log(`[picoTracker] [${state}] ${text}`);
}

function getWasmEnvironmentError() {
  const secure = globalThis.isSecureContext === true;
  const isolated = globalThis.crossOriginIsolated === true;
  const hasSharedMemory = typeof globalThis.SharedArrayBuffer === 'function';
  if (secure && isolated && hasSharedMemory) return null;

  return 'Browser cannot start shared-memory WebAssembly ' +
    `(secure=${secure}, isolated=${isolated}, SharedArrayBuffer=${hasSharedMemory})`;
}

function getAudioState() {
  return currentModule?._PicoTracker_Wasm_GetAudioState?.() ?? 0;
}

function unlockAudio() {
  if (!currentModule || typeof currentModule._PicoTracker_Wasm_UnlockAudio !== 'function') {
    return false;
  }
  const state = getAudioState();
  if (!audioNeedsGesture && state !== 1 && state !== 4) return state === 2 || state === 3;
  const accepted = currentModule._PicoTracker_Wasm_UnlockAudio() !== 0;
  if (accepted) audioNeedsGesture = false;
  return accepted;
}

function setMenuOpen(open) {
  if (!menuToggle || !menuPanel) return;
  menuPanel.hidden = !open;
  menuToggle.setAttribute('aria-expanded', String(open));
  menuToggle.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
}

async function shutdown() {
  if (detachTouchControls) {
    detachTouchControls();
    detachTouchControls = null;
  }
  if (currentInputManager) {
    currentInputManager.detach();
    currentInputManager = null;
  }
  if (currentModule && typeof currentModule._PicoTracker_Wasm_RequestShutdown === 'function') {
    currentModule._PicoTracker_Wasm_RequestShutdown();
  }
  updateStatus('Stopping runtime...', 'pending');
  try {
    if (!await storage.flush()) {
      updateStatus('Storage save failed; retry restart to save changes.', 'error');
      return false;
    }
  } catch (e) {
    console.warn('Storage flush on shutdown error:', e);
    updateStatus('Storage save failed; retry restart to save changes.', 'error');
    return false;
  }
  storage.releaseLock();

  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    const state = currentModule?._PicoTracker_Wasm_GetState?.() ?? 4;
    if (state === 4) { // Stopped
      updateStatus('Stopped', 'pending');
      window.__picoTrackerReady = false;
      return true;
    }
    await new Promise((r) => setTimeout(r, 50));
  }
  updateStatus('Stop timeout', 'error');
  return false;
}

async function restart() {
  if (!await shutdown()) return;
  updateStatus('Restarting tracker...', 'pending');
  window.location.reload();
}

// Storage status subscription
storage.onStateChange((info) => {
  if (!storageStatus) return;
  if (info.state === 'saving') {
    storageStatus.textContent = 'Saving...';
    storageStatus.className = 'storage-tag saving';
  } else if (info.state === 'error') {
    storageStatus.textContent = 'Save Error';
    storageStatus.className = 'storage-tag error';
  } else {
    storageStatus.textContent = 'Saved';
    storageStatus.className = 'storage-tag';
  }
});

// File helpers
function triggerDownload(blobOrUint8Array, filename, mimeType = 'application/zip') {
  const blob = blobOrUint8Array instanceof Blob ? blobOrUint8Array : new Blob([blobOrUint8Array], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function formatBytes(bytes) {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return (bytes / Math.pow(k, i)).toFixed(1) + ' ' + sizes[i];
}

function updateModalStatus(text, isError = false) {
  const el = document.getElementById('modal-status');
  if (!el) return;
  el.textContent = text;
  el.classList.toggle('error', isError);
}

function renderProjects() {
  const projectList = document.getElementById('project-list');
  if (!projectList) return;
  const projects = storage.listProjects();
  let currentProject = null;
  try {
    currentProject = storage.getCurrentProjectName();
  } catch (err) {
    updateModalStatus(err.message, true);
  }
  if (projects.length === 0) {
    projectList.innerHTML = '<div class="empty-message">No projects found in /data/projects.</div>';
    return;
  }
  projectList.innerHTML = '';
  for (const p of projects) {
    const isCurrent = p.name === currentProject;
    const deleteDisabled = isCurrent || currentProject === null;
    const deleteReason = isCurrent
      ? 'Switch projects before deleting the current project.'
      : 'Cannot determine the current project.';
    const row = document.createElement('div');
    row.className = 'file-row';
    row.innerHTML = `
      <div class="file-info">
        <span class="file-name">${escapeHtml(p.name)}</span>
        <span class="file-meta">${isCurrent ? 'Current project · ' : ''}${p.hasDat ? 'Valid project' : 'Invalid (no ptsav.dat)'}</span>
      </div>

      <div class="file-actions">
        <button class="btn btn-sm btn-export-proj" data-name="${escapeHtml(p.name)}">Export .zip</button>
        <button class="btn btn-sm btn-delete-proj" data-name="${escapeHtml(p.name)}" style="color:#ff5555;" ${deleteDisabled ? `disabled title="${deleteReason}"` : ''}>Delete</button>
      </div>
    `;
    projectList.appendChild(row);
  }

  projectList.querySelectorAll('.btn-export-proj').forEach((btn) => {
    btn.addEventListener('click', async (e) => {
      const name = e.currentTarget.dataset.name;
      updateModalStatus(`Exporting project ${name}...`);
      try {
        const zip = await storage.exportProjectZip(name);
        triggerDownload(zip, `${name}.zip`);
        updateModalStatus(`Exported ${name}.zip successfully.`);
      } catch (err) {
        updateModalStatus(`Export failed: ${err.message}`);
      }
    });
  });

  projectList.querySelectorAll('.btn-delete-proj').forEach((btn) => {
    btn.addEventListener('click', async (e) => {
      const name = e.currentTarget.dataset.name;
      try {
        if (name === storage.getCurrentProjectName()) {
          throw new Error('Cannot delete the current project. Switch projects first.');
        }
        if (confirm(`Delete project "${name}"? This cannot be undone.`)) {
          storage.deleteProject(name);
          if (!await storage.flush()) throw new Error(storage.lastError || 'Storage sync failed');
          renderProjects();
          updateModalStatus(`Deleted ${name}.`);
        }
      } catch (err) {
        renderProjects();
        updateModalStatus(`Delete failed: ${err.message}`, true);
      }
    });
  });
}

function renderRenders() {
  const renderList = document.getElementById('render-list');
  if (!renderList) return;
  const renders = storage.listRenders();
  if (renders.length === 0) {
    renderList.innerHTML = '<div class="empty-message">No audio renders found in /data/renders.</div>';
    return;
  }
  renderList.innerHTML = '';
  for (const r of renders) {
    const row = document.createElement('div');
    row.className = 'file-row';
    row.innerHTML = `
      <div class="file-info">
        <span class="file-name">${escapeHtml(r.name)}</span>
        <span class="file-meta">${formatBytes(r.size)}</span>
      </div>
      <div class="file-actions">
        <button class="btn btn-sm btn-dl-render" data-name="${escapeHtml(r.name)}">Download</button>
        <button class="btn btn-sm btn-del-render" data-name="${escapeHtml(r.name)}" style="color:#ff5555;">Delete</button>
      </div>
    `;
    renderList.appendChild(row);
  }

  renderList.querySelectorAll('.btn-dl-render').forEach((btn) => {
    btn.addEventListener('click', (e) => {
      const name = e.currentTarget.dataset.name;
      const blob = storage.getRenderBlob(name);
      triggerDownload(blob, name, 'audio/wav');
    });
  });

  renderList.querySelectorAll('.btn-del-render').forEach((btn) => {
    btn.addEventListener('click', async (e) => {
      const name = e.currentTarget.dataset.name;
      if (confirm(`Delete render "${name}"?`)) {
        storage.deleteRender(name);
        await storage.flush();
        renderRenders();
        updateModalStatus(`Deleted ${name}.`);
      }
    });
  });
}

function switchTab(tabId) {
  document.querySelectorAll('.modal-tab').forEach((tab) => {
    tab.classList.toggle('active', tab.dataset.tab === tabId);
  });
  const tabProjects = document.getElementById('tab-content-projects');
  const tabRenders = document.getElementById('tab-content-renders');
  const tabBackup = document.getElementById('tab-content-backup');
  if (tabProjects) tabProjects.style.display = tabId === 'projects' ? 'block' : 'none';
  if (tabRenders) tabRenders.style.display = tabId === 'renders' ? 'block' : 'none';
  if (tabBackup) tabBackup.style.display = tabId === 'backup' ? 'block' : 'none';
  updateModalStatus('');
  if (tabId === 'projects') renderProjects();
  else if (tabId === 'renders') renderRenders();
}

function openModal() {
  if (filesModal) filesModal.classList.add('open');
  if (currentInputManager) currentInputManager.detach();
  switchTab('projects');
}

function closeModal() {
  if (filesModal) filesModal.classList.remove('open');
  if (currentInputManager) currentInputManager.attach();
}

// Modal event listeners
if (btnFiles) btnFiles.addEventListener('click', openModal);
if (modalCloseBtn) modalCloseBtn.addEventListener('click', closeModal);
if (btnModalDone) btnModalDone.addEventListener('click', closeModal);
if (filesModal) {
  filesModal.addEventListener('click', (e) => {
    if (e.target === filesModal) closeModal();
  });
}
document.querySelectorAll('.modal-tab').forEach((tab) => {
  tab.addEventListener('click', () => switchTab(tab.dataset.tab));
});

const btnImportProject = document.getElementById('btn-import-project');
const inputImportProject = document.getElementById('input-import-project');
if (btnImportProject && inputImportProject) {
  btnImportProject.addEventListener('click', () => inputImportProject.click());
  inputImportProject.addEventListener('change', async (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    updateModalStatus(`Importing ${file.name}...`);
    try {
      const buf = await file.arrayBuffer();
      const name = await storage.importProjectZip(buf, file.name);
      updateModalStatus(`Imported project "${name}" successfully.`);
      renderProjects();
    } catch (err) {
      updateModalStatus(`Import failed: ${err.message}`, true);
    } finally {
      inputImportProject.value = '';
    }
  });
}

const btnRefreshRenders = document.getElementById('btn-refresh-renders');
if (btnRefreshRenders) {
  btnRefreshRenders.addEventListener('click', () => renderRenders());
}

const btnExportBackup = document.getElementById('btn-export-backup');
if (btnExportBackup) {
  btnExportBackup.addEventListener('click', async () => {
    updateModalStatus('Creating full backup...');
    try {
      const zip = await storage.exportFullBackupZip();
      const dateStr = new Date().toISOString().slice(0, 10);
      triggerDownload(zip, `picotracker_backup_${dateStr}.zip`);
      updateModalStatus('Backup downloaded successfully.');
    } catch (err) {
      updateModalStatus(`Backup failed: ${err.message}`);
    }
  });
}

const btnRestoreBackup = document.getElementById('btn-restore-backup');
const inputRestoreBackup = document.getElementById('input-restore-backup');
if (btnRestoreBackup && inputRestoreBackup) {
  btnRestoreBackup.addEventListener('click', () => inputRestoreBackup.click());
  inputRestoreBackup.addEventListener('change', async (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!confirm(`Restore all files from "${file.name}"? Existing files may be overwritten.`)) {
      inputRestoreBackup.value = '';
      return;
    }
    updateModalStatus(`Restoring backup from ${file.name}...`);
    try {
      const buf = await file.arrayBuffer();
      await storage.restoreFullBackupZip(buf);
      updateModalStatus('Workspace restored successfully.');
      renderProjects();
    } catch (err) {
      updateModalStatus(`Restore failed: ${err.message}`);
    } finally {
      inputRestoreBackup.value = '';
    }
  });
}

if (btnLockRetry) {
  btnLockRetry.addEventListener('click', () => window.location.reload());
}

if (btnCredits && creditsDialog && creditsContent) {
  btnCredits.addEventListener('click', async () => {
    creditsContent.textContent = 'Loading credits...';
    creditsDialog.showModal();
    if (currentInputManager) currentInputManager.detach();
    try {
      const response = await fetch('./factory-content-credits.txt');
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      creditsContent.textContent = await response.text();
    } catch (error) {
      creditsContent.textContent = `Could not load credits: ${error.message}`;
    }
  });
  creditsDialog.addEventListener('close', () => {
    if (currentInputManager) currentInputManager.attach();
  });
}

if (menuToggle && menuPanel) {
  menuToggle.addEventListener('click', (event) => {
    event.stopPropagation();
    setMenuOpen(menuPanel.hidden);
  });
  menuPanel.addEventListener('click', (event) => {
    if (event.target.closest('button')) setMenuOpen(false);
  });
  document.addEventListener('click', (event) => {
    if (!event.target.closest('.app-menu')) setMenuOpen(false);
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && !menuPanel.hidden) {
      setMenuOpen(false);
      menuToggle.focus();
    }
  });
}

if (canvas) {
  canvas.addEventListener('click', () => unlockAudio());
}
window.addEventListener('keydown', unlockAudio, { passive: true });
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible') audioNeedsGesture = true;
});

window.addEventListener('beforeunload', (event) => {
  if (storage.pendingGeneration > storage.syncedGeneration) {
    event.preventDefault();
    event.returnValue = '';
  }
});

window.__picoTrackerShutdown = shutdown;
window.__picoTrackerRestart = restart;
window.__picoTrackerUnlockAudio = unlockAudio;
window.__picoTrackerGetAudioState = () => currentModule?._PicoTracker_Wasm_GetAudioState?.() ?? 0;
window.__picoTrackerStorage = storage;

async function boot() {
  try {
    const environmentError = getWasmEnvironmentError();
    if (environmentError) {
      updateStatus(environmentError, 'error');
      return;
    }

    updateStatus('Acquiring storage lock...', 'pending');
    const hasLock = await storage.acquireLock();
    if (!hasLock) {
      document.getElementById('tab-lock-overlay')?.classList.add('open');
      updateStatus('Storage locked by another tab', 'error');
      return;
    }

    updateStatus('Loading WebAssembly module...', 'pending');

    const module = await createPicoTrackerModule({
      canvas: canvas,
      print: (text) => console.log('[STDOUT]', text),
      printErr: (text) => console.error('[STDERR]', text),
      locateFile: (path, prefix) => {
        if (path.endsWith('.wasm')) return (prefix || './wasm/') + path;
        return prefix + path;
      },
      preRun: [(mod) => {
        try {
          mod.FS.mkdir('/data');
        } catch (e) {
          // Ignore if exists
        }
        try {
          mod.FS.mount(mod.IDBFS, {}, '/data');
        } catch (e) {
          console.warn('IDBFS mount warning:', e);
        }
        mod.addRunDependency('idbfs-populate');
        mod.FS.syncfs(true, (err) => {
          if (err) {
            console.warn('IDBFS initial sync error:', err);
          }
          storage.init(mod.FS);
          mod.removeRunDependency('idbfs-populate');
        });
        mod.picoTrackerStorageMutation = () => {
          storage.notifyMutation();
        };
      }],
      onRuntimeInitialized: function () {
        console.log('[picoTracker] Runtime initialized');
        try {
          const installedFactory = storage.seedFactoryContent();
          if (installedFactory) {
            updateStatus('Installing factory content...', 'pending');
            storage.flush().then((ok) => {
              if (!ok) {
                console.warn('Failed to persist factory content');
              }
            });
          }
        } catch (e) {
          console.error('[storage] Failed to install factory content:', e);
        }
        if (typeof this._PicoTracker_Wasm_BootstrapAudio === 'function') {
          this._PicoTracker_Wasm_BootstrapAudio();
        }
      }
    });

    currentModule = module;
    window.__picoTrackerModule = module;

    // Wait for the application to mark ready (State == 1)
    const checkReady = () => {
      if (typeof module._PicoTracker_Wasm_GetState === 'function') {
        const state = module._PicoTracker_Wasm_GetState();
        if (state === 1) { // Ready
          window.__picoTrackerReady = true;

          // Initialize keyboard input and optional mobile touch controls.
          if (currentInputManager) {
            currentInputManager.detach();
          }
          currentInputManager = createInputManager(module);
          currentInputManager.attach();
          if (mobileTouchEnabled) {
            detachTouchControls = attachTouchControls(
              mobileControls,
              currentInputManager,
              { onInteraction: unlockAudio }
            );
          }
          window.__picoTrackerInput = currentInputManager;

          updateStatus('Ready - Advance tracker running', 'ready');
          return;
        } else if (state === 3) { // Failed
          const errPtr = module._PicoTracker_Wasm_GetLastError?.();
          const errMsg = errPtr ? module.UTF8ToString(errPtr) : 'Unknown error';
          updateStatus(`Startup failed: ${errMsg}`, 'error');
          return;
        }
      }
      setTimeout(checkReady, 50);
    };
    checkReady();

    // Helper to capture a 720x720 RGBA frame directly from WASM memory
    window.__picoTrackerCaptureFrameRgba = () => {
      const frameSize = 720 * 720 * 4;
      const ptr = module._malloc(frameSize);
      try {
        module._PicoTracker_Wasm_CaptureFrameRgba(ptr, frameSize);
        return new Uint8Array(module.HEAPU8.buffer, ptr, frameSize).slice();
      } finally {
        module._free(ptr);
      }
    };

  } catch (error) {
    console.error('[picoTracker] Boot error:', error);
    updateStatus(`Error: ${error.message}`, 'error');
  }
}

boot();
