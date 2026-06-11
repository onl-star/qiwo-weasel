#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"
#include "EditSession.h"
#include "QiwoInputFormatter.h"

#include <array>

static weasel::KeyEvent prevKeyEvent;
static BOOL prevfEaten = FALSE;
static int keyCountToSimulate = 0;

namespace {

std::wstring GetDirectRangeText(TfEditCookie ec, ITfRange* range) {
  if (!range) {
    return {};
  }

  std::array<WCHAR, 9> buffer = {};
  ULONG fetched = 0;
  if (FAILED(range->GetText(ec, 0, buffer.data(),
                            static_cast<ULONG>(buffer.size() - 1),
                            &fetched))) {
    return {};
  }
  return std::wstring(buffer.data(), fetched);
}

std::wstring GetTextBeforeRange(TfEditCookie ec, ITfRange* range) {
  if (!range) {
    return {};
  }

  com_ptr<ITfRange> before;
  if (FAILED(range->Clone(&before))) {
    return {};
  }
  before->Collapse(ec, TF_ANCHOR_START);
  LONG shifted = 0;
  if (FAILED(before->ShiftStart(ec, -8, &shifted, nullptr))) {
    return {};
  }
  return GetDirectRangeText(ec, before);
}

std::wstring GetTextAfterRange(TfEditCookie ec, ITfRange* range) {
  if (!range) {
    return {};
  }

  com_ptr<ITfRange> after;
  if (FAILED(range->Clone(&after))) {
    return {};
  }
  after->Collapse(ec, TF_ANCHOR_END);
  LONG shifted = 0;
  if (FAILED(after->ShiftEnd(ec, 8, &shifted, nullptr))) {
    return {};
  }
  return GetDirectRangeText(ec, after);
}

class CDirectFormattedTextEditSession : public CEditSession {
 public:
  CDirectFormattedTextEditSession(com_ptr<WeaselTSF> pTextService,
                                  com_ptr<ITfContext> pContext,
                                  const std::wstring& text,
                                  bool autoCommitSpacingEnabled)
      : CEditSession(pTextService, pContext),
        _text(text),
        _autoCommitSpacingEnabled(autoCommitSpacingEnabled) {}

  bool committed() const { return _committed; }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    TF_SELECTION selection = {};
    ULONG selectionCount = 0;
    if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1,
                                       &selection, &selectionCount)) ||
        selectionCount == 0 || !selection.range) {
      return E_FAIL;
    }

    com_ptr<ITfRange> range;
    range.Attach(selection.range);
    const std::wstring beforeCursor = GetTextBeforeRange(ec, range);
    const std::wstring afterCursor = GetTextAfterRange(ec, range);
    const std::wstring formatted = QiwoInputFormatter::FormatCommitText(
        _text, _autoCommitSpacingEnabled, beforeCursor, afterCursor);
    if (formatted == _text) {
      return S_FALSE;
    }

    if (FAILED(range->SetText(ec, TF_ST_CORRECTION, formatted.c_str(),
                              static_cast<LONG>(formatted.length())))) {
      return E_FAIL;
    }

    range->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION newSelection = {};
    newSelection.range = range;
    newSelection.style.ase = TF_AE_NONE;
    newSelection.style.fInterimChar = FALSE;
    _pContext->SetSelection(ec, 1, &newSelection);
    _committed = true;
    return S_OK;
  }

 private:
  std::wstring _text;
  bool _autoCommitSpacingEnabled;
  bool _committed = false;
};

bool IsDirectFormattableKey(const weasel::KeyEvent& keyEvent) {
  if (keyEvent.mask &
      (ibus::RELEASE_MASK | ibus::CONTROL_MASK | ibus::ALT_MASK)) {
    return false;
  }
  return keyEvent.keycode >= 0x21 && keyEvent.keycode <= 0x7e;
}

}  // namespace

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL* pfEaten,
                                 weasel::KeyEvent* pKeyEvent) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // if server connection is Not OK, don't eat it.
  if (!_EnsureServerConnected()) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    if (pKeyEvent) {
      *pKeyEvent = ke;
    }
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    if (!keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (prevKeyEvent.keycode == ibus::Caps_Lock && prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (keyCountToSimulate)
        keyCountToSimulate--;
    }

    prevfEaten = *pfEaten;
    prevKeyEvent = ke;
  }
}

BOOL WeaselTSF::_TryCommitDirectFormattedText(
    com_ptr<ITfContext> pContext,
    const weasel::KeyEvent& keyEvent) {
  if (!_autoCommitSpacingEnabled || _status.disabled || _status.composing ||
      !IsDirectFormattableKey(keyEvent)) {
    return FALSE;
  }

  const std::wstring text(1, static_cast<wchar_t>(keyEvent.keycode));
  CDirectFormattedTextEditSession* pEditSession = nullptr;
  BOOL committed = FALSE;
  HRESULT hr = E_FAIL;
  if ((pEditSession = new CDirectFormattedTextEditSession(
           this, pContext, text, _autoCommitSpacingEnabled)) != nullptr) {
    const HRESULT requestResult = pContext->RequestEditSession(
        _tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);
    committed = SUCCEEDED(requestResult) && SUCCEEDED(hr) &&
                pEditSession->committed();
    pEditSession->Release();
  }
  return committed;
}

STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground)
    m_client.FocusIn();
  else {
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}

/* Some apps sends strange OnTestKeyDown/OnKeyDown combinations:
 *  Some sends OnKeyDown() only. (QQ2012)
 *  Some sends multiple OnTestKeyDown() for a single key event. (MS WORD 2010
 * x64)
 *
 * We assume every key event will eventually cause a OnKeyDown() call.
 * We use _fTestKeyDownPending to omit multiple OnTestKeyDown() calls,
 *  and for OnKeyDown() to check if the key has already been sent to the server.
 */

STDAPI WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (_fTestKeyDownPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _fTestKeyDownPending = TRUE;
  return S_OK;
}

STDAPI WeaselTSF::OnKeyDown(ITfContext* pContext,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (_fTestKeyDownPending) {
    _fTestKeyDownPending = FALSE;
    *pfEaten = TRUE;
  } else {
    weasel::KeyEvent keyEvent = {};
    _ProcessKeyEvent(wParam, lParam, pfEaten, &keyEvent);
    _UpdateComposition(pContext);
    if (!*pfEaten &&
        _TryCommitDirectFormattedText(pContext, keyEvent)) {
      *pfEaten = TRUE;
    }
  }
  return S_OK;
}

STDAPI WeaselTSF::OnTestKeyUp(ITfContext* pContext,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (_fTestKeyUpPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _fTestKeyUpPending = TRUE;
  return S_OK;
}

STDAPI WeaselTSF::OnKeyUp(ITfContext* pContext,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (_fTestKeyUpPending) {
    _fTestKeyUpPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    if (!_async_edit)
      _UpdateComposition(pContext);
  }
  return S_OK;
}

STDAPI WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                 REFGUID rguid,
                                 BOOL* pfEaten) {
  *pfEaten = FALSE;
  return S_OK;
}

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);

  return (hr == S_OK);
}

void WeaselTSF::_UninitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {}
