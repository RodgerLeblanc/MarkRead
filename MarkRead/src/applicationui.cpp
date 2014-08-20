/*
 * Copyright (c) 2013 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "applicationui.hpp"
#include "Talk2WatchInterface.h"
#include "UdpModule.h"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>
#include <bb/system/InvokeManager>
#include <bb/ApplicationInfo>

using namespace bb::cascades;
using namespace bb::system;

ApplicationUI::ApplicationUI(Application *app):
		QObject(app),
		m_translator(new QTranslator(this)),
		m_localeHandler(new LocaleHandler(this)),
		m_invokeManager(new InvokeManager(this))
{
	// prepare the localization
	if (!QObject::connect(m_localeHandler, SIGNAL(systemLanguageChanged()),
			this, SLOT(onSystemLanguageChanged()))) {
		// This is an abnormal situation! Something went wrong!
		// Add own code to recover here
		qWarning() << "Recovering from a failed connect()";
	}

	// initial load
	onSystemLanguageChanged();

	// Create scene document from main.qml asset, the parent is set
	// to ensure the document gets destroyed properly at shut down.
	QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);

	// Make app available to the qml.
	qml->setContextProperty("app", this);

	// Create root object for the UI
	AbstractPane *root = qml->createRootObject<AbstractPane>();

	// Set created root object as the application scene
	app->setScene(root);

	// By creating Talk2WatchInterface object, it will find what version of T2W is installed
    t2w = new Talk2WatchInterface(this);
    connect(t2w, SIGNAL(transmissionReady()), this, SLOT(onTransmissionReady()));
}

void ApplicationUI::onSystemLanguageChanged() {
	QCoreApplication::instance()->removeTranslator(m_translator);
	// Initiate, load and install the application translation files.
	QString locale_string = QLocale().name();
	QString file_name = QString("MarkRead_%1").arg(locale_string);
	if (m_translator->load(file_name, "app/native/qm")) {
		QCoreApplication::instance()->installTranslator(m_translator);
	}
}

void ApplicationUI::onTransmissionReady()
{
    // Now that T2W version have been found, save it to QSettings

	if (t2w->isTalk2WatchProServiceInstalled() || t2w->isTalk2WatchProInstalled()) {
		settings.setValue("t2wVersion", "Pro");
	}
	else
		if (t2w->isTalk2WatchInstalled())
			settings.setValue("t2wVersion", "Free");
		else
			settings.setValue("t2wVersion", "None");

	// Start headless part after t2wVersion have been saved as this will be needed in headless
	InvokeRequest request;
	request.setTarget("com.RogerLeblanc.MarkReadService");
	request.setAction("com.RogerLeblanc.MarkReadService.START");
	m_invokeManager->invoke(request);
}
