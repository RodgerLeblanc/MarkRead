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

#ifndef SERVICE_H_
#define SERVICE_H_

#include <QObject>
#include <bb/pim/message/MessageService>
#include <bb/pim/message/Message>
#include <bb/pim/message/MessageFilter>
#include <bb/pim/message/MessageUpdate>
#include <QSettings>
#include <QStringList>

namespace bb {
	class Application;
	namespace platform {
		class Notification;
	}
	namespace system {
		class InvokeManager;
		class InvokeRequest;
	}
}

class Talk2WatchInterface;
class UdpModule;
class UpFront;

class Service: public QObject {
	Q_OBJECT
public:
	Service(bb::Application * app);

private slots:
	void handleInvoke(const bb::system::InvokeRequest &);
	void onMessageAdded(bb::pim::account::AccountKey accountId, bb::pim::message::ConversationKey conversationId, bb::pim::message::MessageKey messageId);
    void onTransmissionReady();
    void onUdpDataReceived(QString _data);

private:
    void updateUpFront(const QString &message);
	void authorizeAppWithT2w();
	void markRead(bb::pim::account::AccountKey &accountId, bb::pim::message::MessageKey &messageId);

    bb::platform::Notification* m_notify;
	bb::system::InvokeManager* m_invokeManager;
    bb::pim::message::MessageService* m_messageService;

    UpFront* upFront;
    Talk2WatchInterface* t2w;
    UdpModule* udp;

    QSettings settings;
    QVariantMap settingsMap;

    bool t2wProIsRunning;
    bool actionsCreated;

	bb::pim::account::AccountKey lastAccountId;
    bb::pim::message::MessageKey lastMessageId;
    bb::pim::message::ConversationKey lastConversationId;

    QStringList title, command, description;
};

#endif /* SERVICE_H_ */
