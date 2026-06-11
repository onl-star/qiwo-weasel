#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "QiwoInputFormatter.h"
#include "ResponseParser.h"

#include <array>

namespace {

std::wstring GetRangeText(TfEditCookie ec, ITfRange* range) {
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

std::wstring GetTextBeforeComposition(TfEditCookie ec,
                                      ITfComposition* composition) {
  if (!composition) {
    return {};
  }

  com_ptr<ITfRange> range;
  if (FAILED(composition->GetRange(&range))) {
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

  return GetRangeText(ec, before);
}

std::wstring GetTextAfterComposition(TfEditCookie ec,
                                     ITfComposition* composition) {
  if (!composition) {
    return {};
  }

  com_ptr<ITfRange> range;
  if (FAILED(composition->GetRange(&range))) {
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

  return GetRangeText(ec, after);
}

}  // namespace

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec) {
  // get commit string from server
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  weasel::ResponseParser parser(&commit, context.get(), &_status, &config,
                                &_cand->style());

  bool ok = m_client.GetResponseData(std::ref(parser));

  _UpdateLanguageBar(_status);

  if (ok) {
    _autoCommitSpacingEnabled = config.auto_commit_spacing;
    if (!commit.empty()) {
      // For auto-selecting, commit and preedit can both exist.
      // Commit and close the original composition first.
      if (!_IsComposing()) {
        _StartComposition(_pEditSessionContext,
                          _fCUASWorkaroundEnabled && !config.inline_preedit);
      }
      std::wstring before_cursor;
      std::wstring after_cursor;
      if (config.auto_commit_spacing) {
        before_cursor = GetTextBeforeComposition(ec, _pComposition);
        after_cursor = GetTextAfterComposition(ec, _pComposition);
      }
      commit = QiwoInputFormatter::FormatCommitText(
          commit, config.auto_commit_spacing, before_cursor, after_cursor);
      _InsertText(_pEditSessionContext, commit);
      _EndComposition(_pEditSessionContext, false);
      _committed = TRUE;
    } else {
      _committed = FALSE;
    }
    if (_status.composing && !_IsComposing()) {
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !config.inline_preedit);
    } else if (!_status.composing && _IsComposing()) {
      _EndComposition(_pEditSessionContext, true);
    }
    if (_IsComposing() && config.inline_preedit) {
      _ShowInlinePreedit(_pEditSessionContext, context);
    }
    _UpdateCompositionWindow(_pEditSessionContext);
  }

  _UpdateUI(*context, _status);

  return TRUE;
}
