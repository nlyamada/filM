// 読み込み直後 (本文を描く前) から暗い配色にして、白く光るのを防ぐ。ホストが viewer.html#dark で開く。
if (location.hash === '#dark') document.documentElement.setAttribute('data-theme', 'dark');
