(() => {
  "use strict";
  const button = document.getElementById("share-device");
  const content = document.getElementById("share-content");
  const status = document.getElementById("share-status");
  const fallback = document.getElementById("share-fallback");
  if (!button || !content || !status || !fallback) return;

  function selectForCopy() {
    fallback.hidden = false;
    fallback.open = true;
    content.focus();
    content.select();
    content.setSelectionRange(0, content.value.length);
  }

  function legacyCopy() {
    const active = document.activeElement;
    const selection = window.getSelection();
    const ranges = [];
    if (selection) {
      for (let i = 0; i < selection.rangeCount; ++i) {
        ranges.push(selection.getRangeAt(i).cloneRange());
      }
    }
    selectForCopy();
    let copied = false;
    try {
      copied = document.execCommand("copy");
    } catch (_) {
      copied = false;
    }
    if (copied) {
      fallback.open = false;
      fallback.hidden = true;
      if (active instanceof HTMLElement) active.focus({ preventScroll: true });
      if (selection) {
        selection.removeAllRanges();
        for (const range of ranges) selection.addRange(range);
      }
    }
    return copied;
  }

  button.addEventListener("click", async () => {
    if (!content.value) {
      status.textContent = "设备身份不可用，请刷新后重试。";
      return;
    }
    button.disabled = true;
    status.textContent = "";
    let copied = false;
    if (window.isSecureContext && navigator.clipboard && navigator.clipboard.writeText) {
      try {
        await navigator.clipboard.writeText(content.value);
        copied = true;
      } catch (_) {
        copied = false;
      }
    }
    if (!copied) copied = legacyCopy();
    if (copied) {
      status.textContent = "分享内容已复制，包含连接验证码。";
    } else {
      selectForCopy();
      status.textContent = "浏览器未允许自动复制。内容已选中，请按 Ctrl+C 或长按复制。";
    }
    button.disabled = false;
  });
})();
