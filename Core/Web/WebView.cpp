/***********************************************************************************
** MIT License                                                                    **
**                                                                                **
** Copyright (c) 2017 Victor DENIS (victordenis01@gmail.com)                      **
**                                                                                **
** Permission is hereby granted, free of charge, to any person obtaining a copy   **
** of this software and associated documentation files (the "Software"), to deal  **
** in the Software without restriction, including without limitation the rights   **
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      **
** copies of the Software, and to permit persons to whom the Software is          **
** furnished to do so, subject to the following conditions:                       **
**                                                                                **
** The above copyright notice and this permission notice shall be included in all **
** copies or substantial portions of the Software.                                **
**                                                                                **
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     **
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       **
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    **
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         **
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  **
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  **
** SOFTWARE.                                                                      **
***********************************************************************************/

#include "Web/WebView.hpp"

#include <QHostInfo>
#include <QClipboard>
#include <QTimer>

#include <QUrl>
#include <QUrlQuery>

#include <QSettings>
#include <QAction>

#include <QWebEngineHistory>

#include "Web/WebPage.hpp"
#include "Web/WebHitTestResult.hpp"
#include "Web/Scripts.hpp"

#include "Plugins/PluginProxy.hpp"

namespace Sn {

bool WebView::isUrlValide(const QUrl& url)
{
	return url.isValid() && !url.scheme().isEmpty()
		   && (!url.host().isEmpty() || !url.path().isEmpty() || url.hasQuery());
}

QUrl WebView::searchUrl(const QString& searchText)
{
	QUrl url{QLatin1String("http://www.google.com/search")};
	QUrlQuery urlQuery{};

	urlQuery.addQueryItem(QLatin1String("q"), searchText);
	urlQuery.addQueryItem(QLatin1String("ie"), QLatin1String("UTF-8"));
	urlQuery.addQueryItem(QLatin1String("oe"), QLatin1String("UTF-8"));
	urlQuery.addQueryItem(QLatin1String("client"), QLatin1String("sielo"));

	url.setQuery(urlQuery);

	return url;
}

QList<int> WebView::zoomLevels()
{
	return QList<int>() << 10 << 20 << 30 << 40 << 50 << 60 << 80 << 90 << 100 << 110 << 120 << 130 << 140 << 150 << 160
						<< 170 << 180 << 190 << 200;
}

WebView::WebView(QWidget* parent) :
	QWebEngineView(parent),
	m_currentZoomLevel(zoomLevels().indexOf(100)),
	m_progress(100),
	m_firstLoad(false),
	m_page(nullptr)
{
	connect(this, &QWebEngineView::loadStarted, this, &WebView::sLoadStarted);
	connect(this, &QWebEngineView::loadProgress, this, &WebView::sLoadProgress);
	connect(this, &QWebEngineView::loadFinished, this, &WebView::sLoadFinished);
	connect(this, &QWebEngineView::urlChanged, this, &WebView::sUrlChanged);

	setAcceptDrops(true);
	installEventFilter(this);

	if (parentWidget())
		parentWidget()->installEventFilter(this);
}

WebView::~WebView()
{
	// Empty
}

bool WebView::event(QEvent* event)
{
	if (event->type() == QEvent::ChildAdded) {
		QChildEvent* child_ev{static_cast<QChildEvent*>(event)};

		QOpenGLWidget* widget{static_cast<QOpenGLWidget*>(child_ev->child())};

		if (widget) {
			m_child = widget;
			widget->installEventFilter(this);
		}
	}

	if (event->type() == QEvent::ParentChange && parentWidget())
		parentWidget()->installEventFilter(this);

	return QWebEngineView::event(event);
}

bool WebView::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_child) {
		switch (event->type()) {
		case QEvent::Wheel:
			newWheelEvent(static_cast<QWheelEvent*>(event));
		case QEvent::MouseButtonPress:
			newMousePessEvent(static_cast<QMouseEvent*>(event));
			break;
		case QEvent::MouseButtonRelease:
			newMouseReleaseEvent(static_cast<QMouseEvent*>(event));
			break;
		case QEvent::MouseMove:
			newMouseMoveEvent(static_cast<QMouseEvent*>(event));
			break;
		case QEvent::FocusIn:
			emit focusChanged(true);
			break;
		case QEvent::FocusOut:
			emit focusChanged(false);
			break;
		default:
			break;
		}
	}

	if (watched == parentWidget()) {
		switch (event->type()) {
		case QEvent::KeyPress:
			newKeyPressEvent(static_cast<QKeyEvent*>(event));
			break;
		case QEvent::KeyRelease:
			newKeyReleaseEvent(static_cast<QKeyEvent*>(event));
		default:
			break;
		}
	}

	return QWebEngineView::eventFilter(watched, event);
}

QString WebView::title() const
{
	QString title{QWebEngineView::title()};

	if (title.isEmpty())
		title = url().toString(QUrl::RemoveFragment);

	if (title.isEmpty() || title == QLatin1String("about:blank"))
		return tr("Empty page");

	return title;
}

bool WebView::isTitleEmpty() const
{
	return QWebEngineView::title().isEmpty();
}

WebPage* WebView::page() const
{
	return m_page;
}

void WebView::setPage(WebPage* page)
{
	if (m_page == page)
		return;

	m_page = page;
	QWebEngineView::setPage(page);

	connect(m_page, &WebPage::privacyChanged, this, &WebView::privacyChanged);

	zoomReset();
	initActions();
	Application::instance()->plugins()->emitWebPageCreated(m_page);
}

void WebView::load(const QUrl& url)
{
	QWebEngineView::load(url);

	if (!m_firstLoad) {
		m_firstLoad = true;
	}
}

void WebView::load(LoadRequest& request)
{
	const QUrl requestUrl{request.url()};

	if (requestUrl.isEmpty())
		return;

	if (requestUrl.scheme() == QLatin1String("javascript")) {
		const QString scriptSrc{requestUrl.toString().mid(11)};

		if (scriptSrc.contains(QLatin1Char('%')))
			m_page->runJavaScript(QUrl::fromPercentEncoding(scriptSrc.toUtf8()));
		else
			m_page->runJavaScript(scriptSrc);

		return;
	}

	if (isUrlValide(requestUrl)) {
		loadRequest(requestUrl);
		return;
	}

	if (!requestUrl.isEmpty() && requestUrl.scheme().isEmpty() && !requestUrl.path().contains(QLatin1Char(' '))
		&& !requestUrl.path().contains(QLatin1Char('.'))) {
		QUrl url{QStringLiteral("http://") + requestUrl.path()};

		if (url.isValid()) {
			QHostInfo info{QHostInfo::fromName(url.path())};

			if (info.error() == QHostInfo::NoError) {
				LoadRequest req{request};
				req.setUrl(url);
				loadRequest(req);
				return;
			}
		}
	}

	//TODO: manage search
}

bool WebView::isLoading() const
{
	return m_progress < 100;
}

int WebView::loadingProgress() const
{
	return m_progress;
}

int WebView::zoomLevel() const
{
	return m_currentZoomLevel;
}

void WebView::setZoomLevel(int level)
{
	m_currentZoomLevel = level;
	applyZoom();
}

QPointF WebView::mapToViewport(const QPointF& pos) const
{
	return m_page->mapToViewport(pos);
}

QWidget* WebView::inputWidget() const
{
	if (m_child)
		return m_child;
	else
		return const_cast<WebView*>(this);
}

void WebView::zoomIn()
{
	if (m_currentZoomLevel < zoomLevels().count() - 1) {
		++m_currentZoomLevel;
		applyZoom();
	}
}

void WebView::zoomOut()
{
	if (m_currentZoomLevel > 0) {
		--m_currentZoomLevel;
		applyZoom();
	}
}

void WebView::zoomReset()
{
	QSettings settings{};
	int defaultZoomLevel{settings.value("Preferences/ZoomLevel", zoomLevels().indexOf(100)).toInt()};

	if (m_currentZoomLevel != defaultZoomLevel) {
		m_currentZoomLevel = defaultZoomLevel;
		applyZoom();
	}
}

void WebView::back()
{
	QWebEngineHistory* history{m_page->history()};

	if (history->canGoBack()) {
		history->back();

		emit urlChanged(url());
	}
}

void WebView::forward()
{
	QWebEngineHistory* history{m_page->history()};

	if (history->canGoForward()) {
		history->forward();

		emit urlChanged(url());
	}
}

void WebView::openUrlInNewTab(const QUrl& url, Application::NewTabType type)
{
	loadInNewTab(url, type);
}

void WebView::sLoadStarted()
{
	m_progress = 0;
}

void WebView::sLoadProgress(int progress)
{
	m_progress = progress;
}

void WebView::sLoadFinished(bool ok)
{
	Q_UNUSED(ok);

	m_progress = 100;

	//TODO: Manage history entry
}

void WebView::sUrlChanged(const QUrl& url)
{
	Q_UNUSED(url)

	//TODO: Don't save blank page in history
}

void WebView::openUrlInNewWindow()
{
	//TODO: New window application methode
}

void WebView::copyLinkToClipboard()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		QApplication::clipboard()->setText(action->data().toUrl().toEncoded());
}

void WebView::copyImageToClipboard()
{
	triggerPageAction(QWebEnginePage::CopyImageToClipboard);
}

void WebView::savePageAs()
{
	triggerPageAction(QWebEnginePage::SavePage);
}

void WebView::dlLinkToDisk()
{
	triggerPageAction(QWebEnginePage::DownloadLinkToDisk);
}

void WebView::dlImageToDisk()
{
	triggerPageAction(QWebEnginePage::DownloadImageToDisk);
}

void WebView::dlMediaToDisk()
{
	triggerPageAction(QWebEnginePage::DownloadMediaToDisk);
}

void WebView::openActionUrl()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		load(action->data().toUrl());
}

void WebView::showSiteInformation()
{
	//TODO: Make the site information widget (in utils)
}

void WebView::searchSelectedText()
{
	loadInNewTab(searchUrl(selectedText()), Application::NTT_SelectedTab);
}

void WebView::searchSelectedTextInBgTab()
{
	loadInNewTab(searchUrl(selectedText()), Application::NTT_NotSelectedTab);
}

void WebView::bookmarkLink()
{
	//TODO: Manage bookmarks
}

void WebView::openUrlInSelectedTab()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		openUrlInNewTab(action->data().toUrl(), Application::NTT_CleanSelectedTab);
}

void WebView::openUrlInBgTab()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		openUrlInNewTab(action->data().toUrl(), Application::NTT_CleanNotSelectedTab);
}

void WebView::resizeEvent(QResizeEvent* event)
{
	QWebEngineView::resizeEvent(event);
	emit viewportResized(size());
}

void WebView::contextMenuEvent(QContextMenuEvent* event)
{
	const QPoint position{event->pos()};
	const QContextMenuEvent::Reason reason{event->reason()};

	// My thanks to the duck for asking about lambda
	QTimer::singleShot(0, this, [this, position, reason]()
	{
		QContextMenuEvent ev(reason, position);
		newContextMenuEvent(&ev);
	});
}

void WebView::newWheelEvent(QWheelEvent* event)
{
	if (Application::instance()->plugins()->processWheelEvent(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	if (event->modifiers() & Qt::ControlModifier) {
		event->delta() > 0 ? zoomIn() : zoomOut();
		event->accept();
		return;
	}

	if (event->spontaneous()) {
		const qreal multiplier{Application::wheelScrollLines() / 3.0};

		if (multiplier != 1.0) {
			QWheelEvent newEvent
				{event->pos(), event->globalPos(), event->pixelDelta(), event->angleDelta() * multiplier, 0,
				 Qt::Horizontal, event->buttons(), event->modifiers(), event->phase(), event->source(),
				 event->inverted()};
			Application::sendEvent(m_child, &newEvent);
			event->accept();
		}
	}
}

void WebView::newMousePessEvent(QMouseEvent* event)
{
	m_clickedUrl = QUrl();
	m_clickedPos = QPointF();

	if (Application::instance()->plugins()->processMousePress(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->button()) {
	case Qt::XButton1:
		back();
		event->accept();
		break;
	case Qt::XButton2:
		forward();
		event->accept();
		break;
	case Qt::MiddleButton:
		m_clickedUrl = m_page->hitTestContent(event->pos()).linkUrl();
		if (!m_clickedUrl.isEmpty())
			event->accept();
		break;
	case Qt::LeftButton:
		m_clickedUrl = m_page->hitTestContent(event->pos()).linkUrl();
		break;
	default:
		break;
	}
}

void WebView::newMouseReleaseEvent(QMouseEvent* event)
{
	if (Application::instance()->plugins()->processMouseRelease(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->button()) {
	case Qt::MiddleButton:
		if (!m_clickedUrl.isEmpty()) {
			const QUrl newUrl{m_page->hitTestContent(event->pos()).linkUrl()};

			if (m_clickedUrl == newUrl && isUrlValide(newUrl)) {
				if (event->modifiers() & Qt::ShiftModifier)
					openUrlInNewTab(newUrl, Application::NTT_SelectedTab);
				else
					openUrlInNewTab(newUrl, Application::NTT_NotSelectedTab);

				event->accept();
			}
		}
		break;
	case Qt::LeftButton:
		if (!m_clickedUrl.isEmpty()) {
			const QUrl newUrl{m_page->hitTestContent(event->pos()).linkUrl()};

			if ((m_clickedUrl == newUrl && isUrlValide(newUrl)) && event->modifiers() & Qt::ControlModifier) {
				if (event->modifiers() & Qt::ShiftModifier)
					openUrlInNewTab(newUrl, Application::NTT_SelectedTab);
				else
					openUrlInNewTab(newUrl, Application::NTT_NotSelectedTab);

				event->accept();
			}
		}
	default:
		break;
	}
}

void WebView::newMouseMoveEvent(QMouseEvent* event)
{
	if (Application::instance()->plugins()->processMouseMove(Application::ON_WebView, this, event))
		event->accept();
}

void WebView::newKeyPressEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyPress(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->key()) {
	case Qt::Key_ZoomIn:
		zoomIn();
		event->accept();
		break;
	case Qt::Key_ZoomOut:
		zoomOut();
		event->accept();
		break;
	case Qt::Key_Plus:
		if (event->modifiers() & Qt::ControlModifier) {
			zoomIn();
			event->accept();
		}
		break;
	case Qt::Key_Minus:
		if (event->modifiers() & Qt::ControlModifier) {
			zoomOut();
			event->accept();
		}
		break;
	case Qt::Key_0:
		if (event->modifiers() & Qt::ControlModifier) {
			zoomReset();
			event->accept();
		}
		break;
	case Qt::Key_M:
		if (event->modifiers() & Qt::ControlModifier) {
			m_page->setAudioMuted(!m_page->isAudioMuted());
			event->accept();
		}
		break;

	default:
		break;
	}
}

void WebView::newKeyReleaseEvent(QKeyEvent* event)
{
	if (Application::instance()->plugins()->processKeyRelease(Application::ON_WebView, this, event)) {
		event->accept();
		return;
	}

	switch (event->key()) {
	case Qt::Key_Escape:
		if (isFullScreen()) {
			triggerPageAction(QWebEnginePage::ExitFullScreen);
			event->accept();
		}
		break;

	default:
		break;
	}
}

void WebView::newContextMenuEvent(QContextMenuEvent* event)
{
	Q_UNUSED(event);
}

void WebView::loadRequest(const LoadRequest& request)
{
	if (request.operation() == LoadRequest::GetOp)
		load(request.url());
	else
		m_page->runJavaScript(Scripts::sendPostData(request.url(), request.data()), QWebEngineScript::ApplicationWorld);
}

void WebView::applyZoom()
{
	setZoomFactor(qreal(zoomLevels()[m_currentZoomLevel]) / 100.0);

	emit zoomLevelChanged(m_currentZoomLevel);
}

//TODO: Context menu

void WebView::initActions()
{
	QAction* a_undo{pageAction(QWebEnginePage::Undo)};
	a_undo->setText(tr("&Undo"));
	a_undo->setShortcut(QKeySequence("Ctrl+Z"));
	a_undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_undo->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));

	QAction* a_redo{pageAction(QWebEnginePage::Redo)};
	a_redo->setText(tr("&Redo"));
	a_redo->setShortcut(QKeySequence("Ctrl+Shift+Z"));
	a_redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_redo->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo")));

	QAction* a_cut{pageAction(QWebEnginePage::Cut)};
	a_cut->setText(tr("&Cut"));
	a_cut->setShortcut(QKeySequence("Ctrl+X"));
	a_cut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_cut->setIcon(QIcon::fromTheme(QStringLiteral("edit-cut")));

	QAction* a_copy{pageAction(QWebEnginePage::Copy)};
	a_copy->setText(tr("&Copy"));
	a_copy->setShortcut(QKeySequence("Ctrl+C"));
	a_copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));

	QAction* a_past{pageAction(QWebEnginePage::Paste)};
	a_past->setText(tr("&Paste"));
	a_past->setShortcut(QKeySequence("Ctrl+V"));
	a_past->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_past->setIcon(QIcon::fromTheme(QStringLiteral("edit-paste")));

	QAction* a_selectAll{pageAction(QWebEnginePage::SelectAll)};
	a_selectAll->setText(tr("Select All"));
	a_selectAll->setShortcut(QKeySequence("Ctrl+A"));
	a_selectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	a_selectAll->setIcon(QIcon::fromTheme(QStringLiteral("edit-select-all")));

	QAction* reloadAction{pageAction(QWebEnginePage::Reload)};
	reloadAction->setText(tr("&Reload"));
	reloadAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));

	QAction* stopAction{pageAction(QWebEnginePage::Stop)};
	stopAction->setText(tr("S&top"));
	stopAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));

	addAction(a_undo);
	addAction(a_redo);
	addAction(a_cut);
	addAction(a_copy);
	addAction(a_past);
	addAction(a_selectAll);
}
}