'use strict';

// Keep the completed file operation separate from the follow-up UI refresh.
// A transient scan failure must not turn a successful install into a failure.
async function executeUiAction(work, refresh = null) {
  let value;
  try {
    value = await work();
  } catch (error) {
    return { completed: false, error };
  }

  let refreshError = null;
  if (typeof refresh === 'function') {
    try {
      await refresh();
    } catch (error) {
      refreshError = error;
    }
  }
  return { completed: true, value, refreshError };
}

if (typeof module !== 'undefined' && module.exports) module.exports = { executeUiAction };
else window.executeUiAction = executeUiAction;
