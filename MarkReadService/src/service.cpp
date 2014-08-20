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


#include "service.hpp"
#include "Talk2WatchInterface.h"
#include "UdpModule.h"
#include "UpFront.h"

#include <bb/Application>
#include <bb/ApplicationInfo>
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/system/InvokeManager>
#include <QTimer>

using namespace bb::platform;
using namespace bb::system;

Service::Service(bb::Application * app)	:
		QObject(app),
		m_notify(new Notification(this)),
		m_invokeManager(new InvokeManager(this))
{
	NotificationDefaultApplicationSettings notificationSettings;
	notificationSettings.setPreview(NotificationPriorityPolicy::Allow);
	notificationSettings.apply();

	m_invokeManager->connect(m_invokeManager, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this, SLOT(handleInvoke(const bb::system::InvokeRequest&)));

	// Creates the MessageService object
    m_messageService = new bb::pim::message::MessageService(this);
    // Connect to the messageAdded signal
    connect(m_messageService, SIGNAL(messageAdded(bb::pim::account::AccountKey, bb::pim::message::ConversationKey, bb::pim::message::MessageKey)), SLOT(onMessageAdded(bb::pim::account::AccountKey, bb::pim::message::ConversationKey, bb::pim::message::MessageKey)));

    // Initiate variables
    actionsCreated = false;
	t2wProIsRunning = false;

    // Initiator
    upFront = new UpFront(this);

    t2w = new Talk2WatchInterface(this);
    connect(t2w, SIGNAL(transmissionReady()), this, SLOT(onTransmissionReady()));

    udp = new UdpModule(this);
    udp->listenOnPort(9211);
    connect(udp, SIGNAL(reveivedData(QString)), this, SLOT(onUdpDataReceived(QString)));

    // Save some settings to a local variable to avoid having to read a file (QSettings) every time
    // this app needs to check those settings.
    settingsMap.insert("t2wVersion", settings.value("t2wVersion", "None"));
    settingsMap.insert("autoMarkRead", settings.value("autoMarkRead", "None"));
}

void Service::handleInvoke(const bb::system::InvokeRequest & request) {
    // Not used, but left it here in case needed later
	if (request.action().compare("com.RogerLeblanc.MarkReadService.RESET") == 0) {
	}
}

void Service::onMessageAdded(bb::pim::account::AccountKey accountId, bb::pim::message::ConversationKey conversationId, bb::pim::message::MessageKey messageId)
{
	// Avoid BBM messages (won't work and slows down the hub)
	if (accountId != 13) {
		qDebug() << "TRIGGERED: onMessageAdded accountId: " << accountId << ", conversationId: " << conversationId << ", messageId: " << messageId;

		bb::pim::message::Message message = m_messageService->message(accountId, messageId);

		// Exit if the app isn't inbound
		if (!message.isInbound())
			return;

		// This is saved for later use of manually mark as read function
		lastAccountId = accountId;
		lastMessageId = messageId;
		lastConversationId = conversationId;

		// If user selected to automatically mark as read every message, then ...
		// - If T2W Free is used, mark it as read right away
		// - If T2W Pro is used, mark it as read only if T2W is running (this is checked every minute)
		//   That way, if T2W Pro isn't running, it will not be marked as read as the user didn't
		//   get the message on the watch. Unfortunately, this can't be done with T2W Free.
		if (settingsMap.value("autoMarkRead").toBool()) {
		    QString t2wVersion = settingsMap.value("t2wVersion").toString();
			if (((t2wVersion == "Pro") && (t2wProIsRunning)) || (t2wVersion == "Free"))
			    markRead(accountId, messageId);
		}
	}
}

void Service::markRead(bb::pim::account::AccountKey &accountId, bb::pim::message::MessageKey &messageId)
{
	// Mark as read
	m_messageService->markRead(accountId, messageId);

	// Retrieve infos on this message (for UpFront)
	bb::pim::message::Message message = m_messageService->message(accountId, messageId);

	// Update UpFront
	QString myMessage = "Message from " + message.sender().displayableName() + " marked as read";
	updateUpFront(myMessage);
}

void Service::onTransmissionReady()
{
	// Checks every minute to make sure T2W is still running
	authorizeAppWithT2w();

	if (settingsMap.value("t2wVersion").toString() == "Pro")
	    QTimer::singleShot(60000, this, SLOT(onTransmissionReady()));
}

void Service::authorizeAppWithT2w()
{
    // Default to false
	t2wProIsRunning = false;

	// T2W authorization request --- Limited to Pro version

    if (settingsMap.value("t2wVersion").toString() == "Pro") {
		bb::ApplicationInfo appInfo;
		QString t2wAuthUdpPort = "9211";
		QString description = "Mark email and SMS as read";
		t2w->setAppValues(appInfo.title(), appInfo.version(), appInfo.signingHash(), "UDP", t2wAuthUdpPort, description);
		t2w->sendAppAuthorizationRequest();
	}
}

void Service::onUdpDataReceived(QString _data)
{
	qDebug() << "HL : onUdpDataReceived in..." << _data;
	if(_data=="AUTH_SUCCESS") {
		qDebug() << "Auth_Success!!!";

		// If we received this message, that means T2W Pro is running
		t2wProIsRunning = true;

		// If actions were not created yet, this means the app just started
		// This is done to avoid creating scripts every time we check if T2W Pro is still
		// running (actually every minute)
		if (!actionsCreated) {
		    // This is the list of scripts to be created
			title << "Mark Last Msg Read" << "Delete Last Message" << "Auto Read - ON/OFF";
			command << "MARKREAD_LAST" << "MARKREAD_DELETE" << "MARKREAD_AUTO";
			description << "Mark Last Msg Read" << "Delete Last Message" << "Auto Read - ON/OFF";

			// Create first script
			if (title.size() > 0)
				t2w->createAction(title[0], command[0], description[0]);
		}
		return;
	}

	if (_data=="CREATE_ACTION_SUCCESS") {
		qDebug() << "Create_Action_success";

		// A script have been successfully created
		actionsCreated = true;

		// If there's no more scripts to create, quit
		if (title.size() < 1)
			return;

		// Remove the first item as it was created successfully last time
		title.removeFirst();
		command.removeFirst();
		description.removeFirst();

		// If there's another script remaining, create it
		if (title.size() > 0)
			t2w->createAction(title[0], command[0], description[0]);

		return;
	}

	if (_data=="MARKREAD_LAST") {
		qDebug() << "MARKREAD_LAST";

		// User selected to mark as read last message from ScriptMode

		markRead(lastAccountId, lastMessageId);

		// Retrieve infos on this message for UpFront and message to Pebble
		bb::pim::message::Message message = m_messageService->message(lastAccountId, lastMessageId);

		// This part is tricky and I don't understand it very well (PlainText VS Html). I got that code from the
		// "messages" sample app. All I know is that it works.
		QString body = message.body(bb::pim::message::MessageBody::PlainText).plainText();
	    if (body.isEmpty())
	        body = message.body(bb::pim::message::MessageBody::Html).plainText();

		// Tell user it was marked as read
	    if (!message.sender().displayableName().isEmpty() && !body.isEmpty())
	        t2w->sendSms("Message from " + message.sender().displayableName() + " marked as read", "Here's what was the message : " + body);
	    else
	        t2w->sendSms("No message to mark as read", "MarkRead app don't have any message to mark as read.");
		return;
	}

    if (_data=="MARKREAD_DELETE") {
        qDebug() << "MARKREAD_DELETE";

        // User selected to delete last message from ScriptMode

        // Retrieve infos on this message for UpFront and message to Pebble
        bb::pim::message::Message message = m_messageService->message(lastAccountId, lastMessageId);

        // This part is tricky and I don't understand it very well (PlainText VS Html). I got that code from the
        // "messages" sample app. All I know is that it works.
        QString body = message.body(bb::pim::message::MessageBody::PlainText).plainText();
        if (body.isEmpty())
            body = message.body(bb::pim::message::MessageBody::Html).plainText();

        // Tell user it was marked as read
        if (!message.sender().displayableName().isEmpty() && !body.isEmpty()) {
            t2w->sendSms("Message from " + message.sender().displayableName() + " deleted", "Here's what was the message : " + body);

            // Update UpFront
            QString myMessage = "Message from " + message.sender().displayableName() + " deleted";
            updateUpFront(myMessage);

            // Remove last message (used for email)
            m_messageService->remove(lastAccountId, lastMessageId);
            // Remove last conversation (used for SMS)
            m_messageService->remove(lastAccountId, lastConversationId);
        }
        else
            t2w->sendSms("No message to delete", "MarkRead app don't have any message to delete.");

        return;
    }

    if (_data=="MARKREAD_AUTO") {
		qDebug() << "MARKREAD_AUTO";

        // User selected to switch autoMarkRead value from ScriptMode

		// Save new value
		bool autoMarkRead = !settingsMap.value("autoMarkRead").toBool();
	    settingsMap.insert("autoMarkRead", autoMarkRead);
        settings.setValue("autoMarkRead", autoMarkRead);

        // Send a message to Pebble and UpFront to warn about the new status
		if (autoMarkRead) {
			t2w->sendSms("Automatically Mark As Read -- ON", "Every email or SMS received on Pebble will be automatically marked as read.");
			QString myMessage = "Automatically Mark As Read Enabled";
			updateUpFront(myMessage);
		}
		else {
			t2w->sendSms("Automatically Mark As Read -- OFF", "No email or SMS received will be marked as read, you'll have to mark them manually.");
			QString myMessage = "Automatically Mark As Read Disabled";
			updateUpFront(myMessage);
		}

		return;
	}
}

void Service::updateUpFront(const QString &message)
{
    bb::ApplicationInfo appInfo;
    // Those next images are UGLY and should not be used, they were made in a few minutes only
    QString backgroundZ = "http://s26.postimg.org/e8qt4x3z9/mail_Read.png";
    QString backgroundQ = "http://s26.postimg.org/zdao63cyx/Up_Front_310_211.png";
    QString icon = "http://s26.postimg.org/e8qt4x3z9/mail_Read.png";
    QString seconds = ""; // leave blank
    QString sendToT2w = ""; // leave blank
    QString textColor = "White"; // Choose any color available in QML
    QString other = "";
    QString command = appInfo.signingHash() + "##" + appInfo.title() + "##" + backgroundZ + "##" + backgroundQ + "##" + icon + "##" + message + "##" + seconds + "##" + sendToT2w + "##" + textColor + "##" + other;

    upFront->updateUpFront(command);
}
