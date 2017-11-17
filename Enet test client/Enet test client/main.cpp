#include"enet/enet.h"
#include<cstdlib>
#include<cstdio>
#include<cstring>

#define SERVERADDRESS "10.242.3.221"

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

	printf("create Client Successfully!\n");
	
	return client;
}


int main(int argc, char ** argv)
{

	if (enet_initialize() != 0)
	{
		printf("An error occurred while initializing ENet.\n");
		exit(EXIT_FAILURE);
	}
	while (1) {

		ENetHost* client = createClient();

		ENetAddress address;
		ENetEvent event;
		ENetPeer *peer;

		/* Connect to some.server.net:1234. */
		enet_address_set_host(&address, SERVERADDRESS);
		address.port = 1234;

		/* Initiate the connection, allocating the two channels 0 and 1. */
		peer = enet_host_connect(client, &address, 2, 0);

		if (peer == NULL)
		{
			printf("No available peers for initiating an ENet connection.\n");
			system("pause");
			exit(EXIT_FAILURE);
		}
		/* Wait up to 5 seconds for the connection attempt to succeed. */
		if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
		{
			printf("Connection to %s:1234 succeeded.\n", SERVERADDRESS);
		}
		else
		{
			/* Either the 5 seconds are up or a disconnect event was */
			/* received. Reset the peer in the event the 5 seconds   */
			/* had run out without any significant event.            */
			enet_peer_reset(peer);
			puts("Connection to some.server.net:1234 failed.");
		}

		/* Create a reliable packet of size 7 containing "packet\0" */
		ENetPacket * packet = enet_packet_create(
			"the packet",
			strlen("the packet") + 1,
			ENET_PACKET_FLAG_RELIABLE);

		/* Send the packet to the peer over channel id 0. */
		/* One could also broadcast the packet by         */
		/* enet_host_broadcast (host, 0, packet);         */
		enet_peer_send(peer, 0, packet);

		/* One could just use enet_host_service() instead. */
		enet_host_flush(client);


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
	}

	atexit(enet_deinitialize);
	system("pause");

}