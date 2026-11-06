// SPDX-License-Identifier: BSD-3-Clause

export const ACTIONS = Object.freeze({
  left: 0,
  down: 1,
  right: 2,
  up: 3,
  alt: 4,
  edit: 5,
  enter: 6,
  nav: 7,
  play: 8,
  select: 9,
});

function entry(action, ...bindings) {
  return Object.freeze({
    action,
    bindings: Object.freeze(bindings.map((b) => Object.freeze(b))),
  });
}

// Fixed Advance browser keyboard layout.
// EDIT is mapped to KeyX; NAV is mapped to Slash (/); POWER is omitted.
export const DEFAULT_KEY_MAP = Object.freeze({
  left: entry(ACTIONS.left, ['ArrowLeft'], ['KeyA']),
  down: entry(ACTIONS.down, ['ArrowDown'], ['KeyS']),
  right: entry(ACTIONS.right, ['ArrowRight'], ['KeyD']),
  up: entry(ACTIONS.up, ['ArrowUp'], ['KeyW']),
  alt: entry(ACTIONS.alt, ['KeyZ'], ['AltLeft'], ['AltRight']),
  edit: entry(ACTIONS.edit, ['KeyX']),
  enter: entry(ACTIONS.enter, ['Enter'], ['KeyK']),
  nav: entry(ACTIONS.nav, ['Slash'], ['KeyL'], ['NumpadDivide']),
  play: entry(ACTIONS.play, ['Space'], ['KeyC']),
  select: entry(ACTIONS.select, ['KeyV']),
});

function actionName(action) {
  if (typeof action === 'string' && Object.hasOwn(ACTIONS, action)) return action;
  return Object.entries(ACTIONS).find(([, value]) => value === action)?.[0] ?? null;
}

function normalizeKey(event) {
  if (event.code === 'Slash' || event.key === '/') return 'Slash';
  return event.code;
}

function isEditableTarget(event) {
  const target = event.target;
  if (!target) return false;
  const tag = target.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || target.isContentEditable;
}

export function isMobileTouchBrowser({
  navigator = globalThis.navigator,
  window = globalThis.window,
} = {}) {
  const userAgent = navigator?.userAgent ?? '';
  const isIos = /iPad|iPhone|iPod/i.test(userAgent) ||
    (navigator?.platform === 'MacIntel' && navigator?.maxTouchPoints > 1);
  const isAndroid = /Android/i.test(userAgent);
  const reportsMobile = navigator?.userAgentData?.mobile === true;
  const hasTouch = (navigator?.maxTouchPoints ?? 0) > 0 ||
    (window != null && 'ontouchstart' in window);
  return hasTouch && (isIos || isAndroid || reportsMobile);
}

export function attachTouchControls(root, inputManager, options = {}) {
  if (!root || !inputManager) return () => {};

  const buttons = [...root.querySelectorAll('[data-action]')];
  const activePointers = new Map();

  function releasePointer(pointerId) {
    const active = activePointers.get(pointerId);
    if (!active) return;
    activePointers.delete(pointerId);
    inputManager.release(active.action, active.source);
    if (![...activePointers.values()].some(({ button }) => button === active.button)) {
      active.button.classList.remove('is-pressed');
    }
  }

  function onPointerDown(event) {
    if (event.button !== undefined && event.button !== 0) return;
    const button = event.currentTarget;
    const action = button.dataset.action;
    if (!Object.hasOwn(ACTIONS, action)) return;

    event.preventDefault();
    releasePointer(event.pointerId);
    const source = `touch:${event.pointerId}`;
    activePointers.set(event.pointerId, { action, button, source });
    button.classList.add('is-pressed');
    button.setPointerCapture?.(event.pointerId);
    inputManager.press(action, source);
  }

  function onPointerEnd(event) {
    event.preventDefault();
    const action = activePointers.get(event.pointerId)?.action;
    releasePointer(event.pointerId);
    if (event.type === 'pointerup' && action) {
      options.onInteraction?.(action);
    }
  }

  function preventContextMenu(event) {
    event.preventDefault();
  }

  for (const button of buttons) {
    button.addEventListener('pointerdown', onPointerDown);
    button.addEventListener('pointerup', onPointerEnd);
    button.addEventListener('pointercancel', onPointerEnd);
    button.addEventListener('lostpointercapture', onPointerEnd);
    button.addEventListener('contextmenu', preventContextMenu);
  }

  return () => {
    for (const pointerId of [...activePointers.keys()]) {
      releasePointer(pointerId);
    }
    for (const button of buttons) {
      button.removeEventListener('pointerdown', onPointerDown);
      button.removeEventListener('pointerup', onPointerEnd);
      button.removeEventListener('pointercancel', onPointerEnd);
      button.removeEventListener('lostpointercapture', onPointerEnd);
      button.removeEventListener('contextmenu', preventContextMenu);
      button.classList.remove('is-pressed');
    }
  };
}

export function createInputBridge(module) {
  const setAction = module?._PicoTracker_Wasm_SetAction;
  const repeatAction = module?._PicoTracker_Wasm_RepeatAction;
  const releaseAll = module?._PicoTracker_Wasm_ReleaseAllActions;
  const getActionMask = module?._PicoTracker_Wasm_GetActionMask;
  const getActionGeneration = module?._PicoTracker_Wasm_GetActionGeneration;
  const getLastAction = module?._PicoTracker_Wasm_GetLastAction;

  return Object.freeze({
    pressAction(action) {
      if (typeof setAction === 'function') setAction(action, true);
    },
    releaseAction(action) {
      if (typeof setAction === 'function') setAction(action, false);
    },
    repeatAction(action) {
      if (typeof repeatAction === 'function') repeatAction(action);
    },
    releaseAllActions() {
      if (typeof releaseAll === 'function') releaseAll();
    },
    getActionMask() {
      return typeof getActionMask === 'function' ? getActionMask() : 0;
    },
    getActionGeneration() {
      return typeof getActionGeneration === 'function' ? getActionGeneration() : 0;
    },
    getLastAction() {
      return typeof getLastAction === 'function' ? getLastAction() : 0;
    },
  });
}

export function createInputManager(module, options = {}) {
  const bridge = options.bridge ?? createInputBridge(module);
  const keyMap = options.keyMap ?? DEFAULT_KEY_MAP;
  const listeners = new Set();
  const heldSources = new Map();
  const heldKeys = new Set();
  const activeBindings = new Set();
  let detachListeners = null;

  const publish = () => {
    const snapshot = Object.freeze([...heldSources.keys()]);
    for (const listener of listeners) {
      try {
        listener(snapshot);
      } catch (err) {
        console.error('[picoTracker input listener error]', err);
      }
    }
  };

  function actionId(nameOrAction) {
    const name = actionName(nameOrAction);
    return name === null ? null : keyMap[name]?.action ?? null;
  }

  function press(action, source = 'direct') {
    const name = actionName(action);
    if (name === null) return false;
    const sources = heldSources.get(name) ?? new Set();
    if (sources.has(source)) return true;
    const wasHeld = sources.size > 0;
    sources.add(source);
    heldSources.set(name, sources);
    if (!wasHeld) {
      const id = actionId(name);
      if (id !== null) {
        bridge.pressAction(id);
      }
      publish();
    }
    return true;
  }

  function release(action, source = 'direct') {
    const name = actionName(action);
    if (name === null) return false;
    const sources = heldSources.get(name);
    if (!sources || !sources.has(source)) return false;
    sources.delete(source);
    if (sources.size === 0) {
      heldSources.delete(name);
      const id = actionId(name);
      if (id !== null) {
        bridge.releaseAction(id);
      }
      publish();
    }
    return true;
  }

  function releaseAll() {
    heldSources.clear();
    heldKeys.clear();
    activeBindings.clear();
    bridge.releaseAllActions();
    publish();
  }

  function bindingsForEvent(event) {
    const code = normalizeKey(event);
    return Object.entries(keyMap).flatMap(([name, value]) =>
      value.bindings.map((binding, index) => ({
        name,
        binding,
        source: `keyboard:${name}:${index}`,
      }))
    ).filter(({ binding }) => binding.includes(code));
  }

  function synchronizeBindings() {
    for (const [name, value] of Object.entries(keyMap)) {
      value.bindings.forEach((binding, index) => {
        const source = `keyboard:${name}:${index}`;
        const active = activeBindings.has(source);
        const complete = binding.every((code) => heldKeys.has(code));
        if (complete && !active) {
          activeBindings.add(source);
          press(name, source);
        } else if (!complete && active) {
          activeBindings.delete(source);
          release(name, source);
        }
      });
    }
  }

  function handleKeyDown(event) {
    if (isEditableTarget(event)) return false;
    const bindings = bindingsForEvent(event);
    const consumed = bindings.length > 0;
    if (consumed) {
      event.preventDefault?.();
    }
    // Ignore OS autorepeat; WASM C++ is the single repeat owner
    if (event.repeat || !consumed) return consumed;

    const key = normalizeKey(event);
    heldKeys.add(key);
    synchronizeBindings();
    return true;
  }

  function handleKeyUp(event) {
    if (isEditableTarget(event)) return false;
    const bindings = bindingsForEvent(event);
    const consumed = bindings.length > 0;
    if (consumed) {
      event.preventDefault?.();
    }

    const key = normalizeKey(event);
    heldKeys.delete(key);
    synchronizeBindings();
    return consumed;
  }

  function attach({ target = globalThis.window, document = globalThis.document } = {}) {
    if (detachListeners) detachListeners();

    let attached = true;
    const onKeyDown = (event) => attached && handleKeyDown(event);
    const onKeyUp = (event) => attached && handleKeyUp(event);
    const onBlur = () => { if (attached) releaseAll(); };
    const onPageHide = () => { if (attached) releaseAll(); };
    const onVisibilityChange = () => {
      if (attached && document?.visibilityState !== 'visible') {
        releaseAll();
      }
    };

    target?.addEventListener?.('keydown', onKeyDown);
    target?.addEventListener?.('keyup', onKeyUp);
    target?.addEventListener?.('blur', onBlur);
    target?.addEventListener?.('pagehide', onPageHide);
    document?.addEventListener?.('visibilitychange', onVisibilityChange);

    detachListeners = () => {
      if (!attached) return;
      attached = false;
      target?.removeEventListener?.('keydown', onKeyDown);
      target?.removeEventListener?.('keyup', onKeyUp);
      target?.removeEventListener?.('blur', onBlur);
      target?.removeEventListener?.('pagehide', onPageHide);
      document?.removeEventListener?.('visibilitychange', onVisibilityChange);
      releaseAll();
      detachListeners = null;
    };

    return detachListeners;
  }

  function detach() {
    if (detachListeners) {
      detachListeners();
    }
  }

  return Object.freeze({
    press,
    release,
    releaseAll,
    handleKeyDown,
    handleKeyUp,
    attach,
    detach,
    getHeldActions: () => [...heldSources.keys()],
    subscribe(listener) {
      listeners.add(listener);
      listener(Object.freeze([...heldSources.keys()]));
      return () => listeners.delete(listener);
    },
    bridge,
  });
}
