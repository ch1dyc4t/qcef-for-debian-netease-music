// Copyright (c) 2017 Deepin Ltd. All rights reserved.
// Use of this source is governed by General Public License that can be found
// in the LICENSE file.

#include "widgets/qcef_client_handler_delegate.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/qcef_web_channel_consts.h"
#include "include/cef_origin_whitelist.h"
#include "qcef_browser_event_delegate.h"
#include "qcef_browser_event_delegate_p.h"
#include "widgets/qcef_web_settings.h"

QCefClientHandlerDelegate::QCefClientHandlerDelegate(QCefWebPage* web_page)
    : web_page_(web_page) {
}

QCefClientHandlerDelegate::~QCefClientHandlerDelegate() {
  if (cef_browser_ != nullptr) {
    cef_browser_->GetHost()->CloseBrowser(false);
//    cef_browser_->Release();
    cef_browser_ = nullptr;
  }
  if (context_menu_ != nullptr) {
    delete context_menu_;
    context_menu_ = nullptr;
  }
}

bool QCefClientHandlerDelegate::OnBeforePopup(
    const CefString& target_url,
    CefLifeSpanHandler::WindowOpenDisposition target_dispositio) {
  if (cef_browser_ != nullptr) {
    const QUrl url(target_url.ToString().c_str());
    QCefWindowOpenDisposition disposition =
        static_cast<QCefWindowOpenDisposition>(target_dispositio);
    return web_page_->getEventDelegate()->onBeforePopup(url, disposition);
  } else {
    return true;
  }
}

void
QCefClientHandlerDelegate::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  if (cef_browser_ == nullptr) {
    cef_browser_ = browser;
  }

  // Set Cross-Origin white list.
  const auto white_list = web_page_->settings()->crossOriginWhiteList();
  for (const QCefWebSettings::CrossOriginEntry& entry : white_list) {
    qDebug() << "Add cross-origin white entry:" << entry.source << entry.target;
    CefAddCrossOriginWhitelistEntry(entry.source.toString().toStdString(),
                                    entry.target.scheme().toStdString(),
                                    entry.target.host().toStdString(),
                                    true);
  }
}

void QCefClientHandlerDelegate::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (cef_browser_->GetIdentifier() == browser->GetIdentifier()) {
    cef_browser_ = nullptr;
    // TODO(LiuLang): Emit close signal.
  }
}

void QCefClientHandlerDelegate::OnFaviconURLChange(const CefString& icon_url,
                                                   CefRefPtr<CefImage> icon) {
  QPixmap pixmap;
  int pixel_width = 0;
  int pixel_height = 0;
  const int scale_factor = 1;
  CefRefPtr<CefBinaryValue> binary = icon->GetAsPNG(scale_factor, true,
                                                    pixel_width, pixel_height);
  if (binary != nullptr) {
    const size_t image_size = binary->GetSize();
    unsigned char* data = (unsigned char*)malloc(image_size);
    const size_t read = binary.get()->GetData(data, image_size, 0);
    pixmap.loadFromData(data, static_cast<uint>(read));
    free(data);
  }
  const QUrl url(QString::fromStdString(icon_url));
  if (!pixmap.isNull()) {
    QIcon qicon(pixmap);
    web_page_->updateFavicon(url, qicon);
  } else {
    web_page_->updateFavicon(url, QIcon());
  }
}

void QCefClientHandlerDelegate::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() == cef_browser_->GetIdentifier()) {
    web_page_->onBrowserGotFocus();
  }
}

void QCefClientHandlerDelegate::OnLoadStarted(CefRefPtr<CefBrowser> browser,
                                              CefRefPtr<CefFrame> frame) {
  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() == cef_browser_->GetIdentifier() &&
      browser->GetMainFrame()->GetIdentifier() == frame->GetIdentifier()) {
    emit web_page_->loadStarted();
  }
}

void QCefClientHandlerDelegate::OnLoadingStateChange(
    CefRefPtr<CefBrowser> browser,
    bool is_loading,
    bool can_go_back,
    bool can_go_forward) {
  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() == cef_browser_->GetIdentifier()) {
    emit web_page_->loadingStateChanged(is_loading,
                                        can_go_back,
                                        can_go_forward);
  }
}

void QCefClientHandlerDelegate::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          int httpStatusCode) {
  Q_UNUSED(httpStatusCode);
  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() == cef_browser_->GetIdentifier() &&
      browser->GetMainFrame()->GetIdentifier() == frame->GetIdentifier()) {
    emit web_page_->loadFinished(true);
  }
}

QString QCefClientHandlerDelegate::OnLoadError(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int errorCode) {
  Q_UNUSED(errorCode);
  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() == cef_browser_->GetIdentifier() &&
      browser->GetMainFrame()->GetIdentifier() == frame->GetIdentifier()) {
    emit web_page_->loadFinished(false);
  }

  // TODO(LiuLang): Pass |errorCode|.
  return web_page_->pageErrorContent();
}

bool QCefClientHandlerDelegate::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  Q_UNUSED(source_process);

  if (cef_browser_ != nullptr &&
      browser->GetIdentifier() != cef_browser_->GetIdentifier()) {
    return false;
  }

  const QString name(message->GetName().ToString().c_str());
  if (name == kQCefRenderContextCreated) {
    web_page_->createTransportChannel();
    emit web_page_->renderContextCreated();
    return true;
  }
  if (name == kQCefRenderContextReleased) {
    web_page_->releaseTransportChannel();
    return true;
  }
  if (name == kQCefRenderQtMessage) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args->GetSize() != 1) {
      qWarning() << __FUNCTION__ << "args size mismatch, expect 1, got"
                 << args->GetSize();
      return false;
    }
    const QString msg = QString::fromStdString(args->GetString(0));
    qDebug() << __FUNCTION__ << " message :" << msg;
    const QJsonDocument doc(QJsonDocument::fromJson(msg.toUtf8()));
    if (doc.isObject()) {
      web_page_->handleWebMessage(doc.object());
    } else {
      qWarning() << __FUNCTION__ << " invalid json message:" << msg;
    }
    return true;
  }

  // Web Notification.
  if (name == kQCefWebNotificationBody) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args->GetSize() < 2) {
      qCritical() << "Invalid web notification body, parameters mismatch";
      return false;
    }
    const std::string url = args->GetString(0);
    const std::string body = args->GetString(1);
    qCritical() << "Web notification" << url.c_str() << body.c_str();

    if (args->GetSize() > 2) {
      // Parse notification option.
      CefRefPtr<CefDictionaryValue> dict = args->GetDictionary(2);
      CefDictionaryValue::KeyList keys;
      if (dict->GetKeys(keys)) {
        for (const CefString& key : keys) {
          CefString value = dict->GetString(key);
          qCritical() << "delegate, key:" << key.ToString().c_str()
                      << ", value:" << value.ToString().c_str();
        }
      }
    }

    return true;
  }

  return false;
}

void QCefClientHandlerDelegate::OnSetFullscreen(bool fullscreen) {
  emit web_page_->fullscreenRequested(fullscreen);
}

void QCefClientHandlerDelegate::OnTitleChanged(const CefString& title) {
  web_page_->updateTitle(QString::fromStdString(title));
}

void QCefClientHandlerDelegate::OnUrlChanged(const CefString& url) {
  web_page_->updateUrl(QUrl(QString::fromStdString(url)));
}

bool QCefClientHandlerDelegate::OnPreKeyEvent(const QKeyEvent& event) {
  auto event_delegate = web_page_->getEventDelegate();
  if (event_delegate != nullptr) {
    return event_delegate->onPreKeyEvent(event);
  } else {
    return false;
  }
}

bool QCefClientHandlerDelegate::OnBeforeBrowse(const CefString& url,
                                               bool is_redirect) {
  auto event_delegate = web_page_->getEventDelegate();
  if (event_delegate != nullptr) {
    const QUrl qUrl(QString::fromStdString(url.ToString()));
    return event_delegate->onBeforeBrowse(qUrl, is_redirect);
  } else {
    return false;
  }
}

void QCefClientHandlerDelegate::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {
  Q_UNUSED(browser);
  Q_UNUSED(frame);
  auto event_delegate = web_page_->getEventDelegate();
  if (event_delegate != nullptr) {
    QCefContextMenuParams qcef_params;
    qcef_params.p->params = params;
    if (context_menu_ == nullptr) {
      context_menu_ = new QCefContextMenu();
    }
    context_menu_->clear();
    event_delegate->onBeforeContextMenu(web_page_, context_menu_, qcef_params);
    model->Clear();
    for (const QCefContextMenu::MenuItem& item : context_menu_->items()) {
      switch (item.type) {
        case QCefContextMenu::ItemType::Separator: {
          model->AddSeparator();
          break;
        }
        case QCefContextMenu::ItemType::Item: {
          model->AddItem(item.id, item.label.toStdString());
          break;
        }
        default: {
          break;
        }
      }
    }
  }
}

bool QCefClientHandlerDelegate::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    int command_id) {
  Q_UNUSED(browser);
  Q_UNUSED(frame);
  Q_UNUSED(params);
  if (context_menu_ != nullptr) {
    if (context_menu_->callbacks().contains(command_id)) {
      context_menu_->callbacks().value(command_id)(web_page_);
      return true;
    }
  }
  return false;
}
