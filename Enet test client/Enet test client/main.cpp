#include"enet/enet.h"
#include<cstdlib>
#include<cstdio>
#include<cstring>
#include<thread>
#include<vector>
#include<Windows.h>
using namespace std;

#define SERVERADDRESS "10.242.3.221"
#define CLIENTMAXNUMBER 3000

ENetHost* clients[CLIENTMAXNUMBER];
ENetPeer* peers[CLIENTMAXNUMBER];
int createSuccess = 0;
int connectSuccess = 0;
int connectFailure = 0;

ENetHost* createClient() {
	ENetHost * client;
	client = enet_host_create(
		NULL /* create a client host */,
		1 /* only allow 1 outgoing connection */,
		2 /* allow up 2 channels to be used, 0 and 1 */,
		57600 / 8 /* 56K modem with 56 Kbps downstream bandwidth */,
		14400 / 8 /* 56K modem with 14 Kbps upstream bandwidth */
	);

	if (client == NULL)
	{
		printf("An error occurred while trying to create an ENet client host.\n");
		exit(EXIT_FAILURE);
	}

	createSuccess++;
	
	return client;
}

void createClients() {
	memset(clients, 0, sizeof(ENetHost*) * CLIENTMAXNUMBER);
	memset(peers, 0, sizeof(ENetPeer*) * CLIENTMAXNUMBER);

	for (int i = 0; i < CLIENTMAXNUMBER; i++) {
		clients[i] = createClient();
	}

}

void connectServer(int num,ENetAddress address) {
	ENetEvent event;
	ENetPeer *peer;
	ENetHost* client = clients[num];
	/* Initiate the connection, allocating the two channels 0 and 1. */
	peers[num] = peer = enet_host_connect(client, &address, 2, 0);

	if (peer == NULL)
	{
		printf("No available peers for initiating an ENet connection.\n");
		system("pause");
		exit(EXIT_FAILURE);
	}

	bool connected = false;
	int whiletimes = 0;

	while (!connected) {
		whiletimes++;

		if (enet_host_service(client, &event, 3000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
		{
			connected = true;
			printf("client %d Connection to %s:1234 succeeded.while times: %d\n", num, SERVERADDRESS,whiletimes);
			connectSuccess++;
		}
		//else
		//{
		//	/* Either the 5 seconds are up or a disconnect event was */
		//	/* received. Reset the peer in the event the 5 seconds   */
		//	/* had run out without any significant event.            */
		//	enet_peer_reset(peer);
		//	printf("client %d Connection to %s:1234 failed.\n", num, SERVERADDRESS);
		//	connectFailure++;
		//}

		/* Create a reliable packet of size 7 containing "packet\0" */
		ENetPacket * packet = enet_packet_create(
			"success connected!haha!",
			strlen("success connected!haha!") + 1,
			ENET_PACKET_FLAG_RELIABLE);

		/* Send the packet to the peer over channel id 0. */
		/* One could also broadcast the packet by         */
		/* enet_host_broadcast (host, 0, packet);         */
		enet_peer_send(peer, 0, packet);

		/* One could just use enet_host_service() instead. */
		enet_host_flush(client);

	}

}



int main(int argc, char ** argv)
{

	if (enet_initialize() != 0)
	{
		printf("An error occurred while initializing ENet.\n");
		exit(EXIT_FAILURE);
	}
	ENetAddress address;
	/* Connect to some.server.net:1234. */
	enet_address_set_host(&address, SERVERADDRESS);
	address.port = 1234;

	createClients();
	printf("%d clients crtate sucessfully\n", createSuccess);

	vector<thread> ths;
	for (int i = 0; i < CLIENTMAXNUMBER; i++) {
		ths.push_back( thread(&connectServer, i, address) );
	}
	for (auto& th : ths) {
		th.join();
	}

	printf("success: %d, failure: %d\n", connectSuccess, connectFailure);
	

	atexit(enet_deinitialize);
	system("pause");

}

//enet_peer_disconnect(peer, 0);
///* Allow up to 3 seconds for the disconnect to succeed
//* and drop any packets received packets.
//*/
//while (enet_host_service(client, &event, 3000) > 0)
//{
//	switch (event.type)
//	{
//	case ENET_EVENT_TYPE_RECEIVE:
//		enet_packet_destroy(event.packet);
//		break;
//	case ENET_EVENT_TYPE_DISCONNECT:
//		puts("Disconnection succeeded.");
//		break;
//	}
//}
///* We've arrived here, so the disconnect attempt didn't */
///* succeed yet.  Force the connection down.             */
//enet_peer_reset(peer);

//enet_host_destroy(client);