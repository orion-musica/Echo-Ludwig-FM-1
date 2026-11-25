/* Shared by every page: status messages and the remembered display options. */
const root = document.documentElement;
const status = document.getElementById('status');
const say = msg => { status.textContent = ''; setTimeout(() => { status.textContent = msg; }, 50); };
/* ---------- display options (remembered) ---------- */
let prefs = {};
try { prefs = JSON.parse(localStorage.getItem('echo-site') || '{}'); } catch (e) {}
const save = () => { try { localStorage.setItem('echo-site', JSON.stringify(prefs)); } catch (e) {} };
const setClass = (prefix, value) => { [...root.classList].filter(c => c.startsWith(prefix)).forEach(c => root.classList.remove(c)); root.classList.add(prefix + value); };
for (const r of document.querySelectorAll('input[name=size]')) {
  r.checked = r.value === String(prefs.size || 1);
  r.addEventListener('change', () => { prefs.size = r.value; setClass('s-', r.value); save(); say('Text size ' + r.parentElement.textContent.trim()); });
}
for (const r of document.querySelectorAll('input[name=theme]')) {
  r.checked = r.value === (prefs.theme || 'auto');
  r.addEventListener('change', () => { prefs.theme = r.value; setClass('t-', r.value); save(); say('Colours: ' + r.parentElement.textContent.trim()); });
}
const wide = document.getElementById('wide');
wide.checked = !!prefs.wide;
wide.addEventListener('change', () => { prefs.wide = wide.checked; root.classList.toggle('wide', wide.checked); save(); say(wide.checked ? 'Wide spacing on' : 'Wide spacing off'); });

