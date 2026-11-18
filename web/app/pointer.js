// A drag follows the finger (or mouse) that began it, and no other: with a key held under one
// finger and a knob turned with another, each goes its own way. It ends when that pointer
// lifts, or when the system takes the touch back (pointercancel).
export function followPointer(down, move, up) {
  const id = down?.pointerId;
  const mine = (e) => id == null || e.pointerId === id;
  const m = (e) => { if (mine(e)) move(e); };
  const u = (e) => {
    if (!mine(e)) return;
    window.removeEventListener('pointermove', m);
    window.removeEventListener('pointerup', u);
    window.removeEventListener('pointercancel', u);
    up(e);
  };
  window.addEventListener('pointermove', m);
  window.addEventListener('pointerup', u);
  window.addEventListener('pointercancel', u);
}
