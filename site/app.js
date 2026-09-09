// ==========================================================================
// NYKON OS - OFFICIAL SITE SCRIPT
// ==========================================================================

document.addEventListener('DOMContentLoaded', () => {
  // Global Theme Management
  const themeToggleBtns = document.querySelectorAll('.theme-toggle-btn');
  const htmlEl = document.documentElement;
  
  function setTheme(theme) {
    htmlEl.setAttribute('data-theme', theme);
    localStorage.setItem('nykon_theme', theme);
    themeToggleBtns.forEach(btn => {
      btn.innerHTML = theme === 'light' ? '🌙' : '☀️';
      btn.setAttribute('aria-label', `Switch to ${theme === 'light' ? 'Dark' : 'Light'} Mode`);
    });
  }
  
  const savedTheme = localStorage.getItem('nykon_theme') || 'dark';
  setTheme(savedTheme);
  
  themeToggleBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const current = htmlEl.getAttribute('data-theme') || 'dark';
      setTheme(current === 'dark' ? 'light' : 'dark');
    });
  });
});
