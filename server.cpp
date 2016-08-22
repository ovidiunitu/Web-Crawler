
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>

#include <dirent.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <queue>
#include <vector>
#include <map>
#include <set>
#include <errno.h>

#include <stdint.h>
#include <ostream>

#define STDOUT 1
#define ERROR 0

#define MAXLEN 5000
#define MAX_CLIENTS 15
#define HTTP_PORT 80
#define BUFLEN 50000
#define DESCARCARE 1
#define FREE 2
#define LINK_NOU 3
#define RESURSA_DESCARCATA 4
#define MAX_DEPTH 4
#define MIN_CLIENTS 5

using namespace std;


struct sockaddr_in serv_addr;
bool recursiv, everything, output;
bool is_port, is_outputlog;
bool commmand_in_progres, closed_server;
string fisier_log;
string fisier_log_stdout;
string fisier_log_stderr;
fstream f, g;
int port, sockfd, fdmax = -1;


string indexhtml("index.html");


fd_set read_fds;	//multimea de citire folosita in select()
fd_set tmp_fds;	//multime folosita temporar


class cmp
{
public:
	bool operator () (pair <string, int > a, pair <string, int > b)
	{
		return a.second > b.second;
	}
};


queue<int> clienti_liberi;
vector< pair <struct sockaddr_in, int > > clienti;
priority_queue < pair <string, int >, vector<pair <string, int > >, cmp  > links;

vector<string> downloaded_resources;
vector<string> in_download;
set<int> closed_clients;


// Citeste maxim maxlen octeti din socket-ul sockfd. Intoarce
// numarul de octeti cititi.

ssize_t Readline(int sockd, char *vptr, size_t maxlen) {
	char    c, *buffer;

	buffer = vptr;
	int i;
	for ( i = 0 ; i < (int) maxlen ; i++)
	{
		int primit = recv(sockd, &c, 1, 0 );
		if (primit < 0 )
			return -1;
		if (primit == 0)
		{
			cout << "readline primesc 0 de ce??????\n\n\n";
		}
		(*buffer) = c;
		buffer++;
	}


	return i;
}



// Scrie maxim dim octeti din socket-ul serv. Intoarce
// numarul de octeti cititi.
int sendInfo(int serv, char * info, int dim)
{
	char    c, *buffer;

	buffer = info;
	int i;
	for ( i = 0 ; i < (int) dim ; i++)
	{
		c = *buffer;
		int sent = send(serv, &c, 1, 0 );
		if (sent < 0 )
			return -1;
		if (sent == 0)
		{
			cout << "sendInfo primesc 0 de ce??????\n\n\n";
		}
		buffer++;
	}

	return i;
}


void error(const char *msg)
{
	perror(msg);
	exit(1);
}

void print_output(int out, char* mesaj) // 1 pentru stdout 0 pt sdt err
{
	if (is_outputlog )
	{
		if (out)
			f << mesaj << flush;
		else
			g << mesaj << flush;
		return;
	}
	if (out)
		cout << mesaj << flush;
	else
		cerr << mesaj << flush;
}

// aflu portul cu care este rulat serverul
void alfa_port(char port_string[])
{
	port = 0;
	int i;
	// construiesc numarul
	for (i = 0; i < (int)strlen(port_string); i++)
	{
		if (isdigit(port_string[i]) == 0)
		{
			is_port = false;
			return;
		}
		port = port * 10 +  (port_string[i] - '0');
	}
	is_port = true;
}
// aflu fisierele de log
void alfa_fisierlog(char fisier[])
{
	fisier_log = string(fisier);
	is_outputlog = true;
}
// determin cu ce argumente este rulat serverul
void determina_argumete(int argc, char *argv[])
{
	int i;
	recursiv = false;
	everything =  false;
	is_port =  false;
	is_outputlog = false;
	fisier_log = "";

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-p")) // trebuie sa urmeze port
			if (i + 1 < argc)
			{
				alfa_port(argv[i + 1]);
				i++;
				continue;
			}
		if (!strcmp(argv[i], "-o")) // trebuie sa urmeze nume fisier de output
			if (i + 1 < argc)
			{
				alfa_fisierlog(argv[i + 1]);
				i++;
				continue;
			}
		if (!strcmp(argv[i], "-e"))  // comanda everything
		{
			everything = true;
			continue;
		}
		if (!strcmp(argv[i], "-r"))  // comanda recursive
		{
			recursiv = true;
			continue;
		}
	}
	if (is_outputlog)// daca am fisiere de output, le crees
	{
		fisier_log_stdout = fisier_log + ".stdout";
		fisier_log_stderr = fisier_log + ".stderr";
		f.open(fisier_log_stdout.c_str(), ios::out);
		g.open(fisier_log_stderr.c_str(), ios::out);
	}

}


// printez la output pentru a vedea efectul comenzii
// deoarece eu scriu in fisier multe informatii si va fi
// greu sa le observam
void status() {

	int i;
	// parcurg fiecare client si afisez informatii despre el
	for (i = 0; i < (int)clienti.size(); i++)
	{
		struct sockaddr_in cl =  clienti[i].first;

		char temp[BUFLEN];
		char ip[BUFLEN];
		// determin adresa ip
		sprintf(ip, "%d.%d.%d.%d",
		        int(cl.sin_addr.s_addr & 0xFF),
		        int((cl.sin_addr.s_addr & 0xFF00) >> 8),
		        int((cl.sin_addr.s_addr & 0xFF0000) >> 16),
		        int((cl.sin_addr.s_addr & 0xFF000000) >> 24));

		sprintf(temp, "IP: %s\tPort: %d\n", ip, (int)cl.sin_port);
		// o afisez ca output normal, la stdout (sau in fisier)

		cout << temp << flush;
		if (fisier_log != "")
			print_output(STDOUT, temp); // daca vrem in fisier
	}

	// afisez informatie suplimentara
	char temp[BUFLEN];
	sprintf(temp, "Clienti liberi: %d \n Numarul de linkuri de descarcat: %d \n",
	        (int)clienti_liberi.size(), (int)links.size());


	print_output(STDOUT, temp);
}


void download(char buffer[]) {
	// pentru un link primit de la tastatura  il introduc in coada de prioritati
	// cu adancimea 0
	links.push(make_pair(string(buffer), 0));
}

void close_server() {
	closed_server = true;
	// inchid serverul
	// trimit fiecarui client mesajul de iesire 'e'
	for (int i = 0; i < (int) clienti.size(); i++)
	{
		int socket_client =  clienti[i].second;
		char ch = 'e';
		int sent =  send(socket_client, &ch, 1, 0);
		if (sent < 0 )
		{
			print_output(ERROR, (char *)"Nu am putut sa trimit la un client");
			//exit(1);
			return ;
		}
		FD_CLR(socket_client, &read_fds);

	}
	// scot stdin din lista de socketi pe care ascult
	FD_CLR(0, &read_fds);
}

void analizeaza_comanda(char buffer[])
{
	// analizez o comanda primita de la utilizator (de la tastatura)
	stringstream sin(buffer);
	string s;
	sin >> s;
	if (s == "status")
		status();
	else if (s == "download")
	{
		char temp[BUFLEN];
		sin >> temp;
		download(temp);
	}
	else if (s == "exit")
		close_server();
	else
		print_output(ERROR, (char *)"Comanda invalida!\n");


}
void new_conection() {
	// s-a conectat un nou client

	struct sockaddr_in cli_addr;
	int client_len = sizeof(cli_addr);
	int newsockfd;
	// socket nou
	newsockfd = accept(sockfd, (struct  sockaddr*)&cli_addr, (socklen_t*)&client_len);
	if (newsockfd == -1)
	{
		error("ERROR in accept");
		print_output(ERROR, (char *)"Eroare in accept\n");
		return ;
	}

	FD_SET(newsockfd, &read_fds);
	// vector de clienti
	clienti.push_back(make_pair(cli_addr, newsockfd));

	// il introduc in lista in care ascult
	if (newsockfd > fdmax)
		fdmax = newsockfd;
	// este liber
	clienti_liberi.push(newsockfd);
	char temp[BUFLEN];
	// afisez ca s-a conectat un client nou
	sprintf(temp, "Noua conexiune de la %d \n", newsockfd);
	print_output(STDOUT, temp);

}

// functie care imi ia ultimele l caractere din stringul str
string take_last (string str, int l)
{
	if ((int)str.size() < l)// daca stringul nu are l caractere
		return ""; // returnez nimic
	string rez = "";
	int i;

	for (i = str.size() - l ; i < (int)str.size() ; i++)
		rez = rez + str[i];

	return rez;
}

// functia imi ia un link si il transforma astfel incat sa nu avem
// linkuri cu ../ sau ./
// acest lucru este necesar deoarece eu retin ce fisiere am descarcat

string transforma(string info)
{

	char temp[BUFLEN] = {'\0'};
	char temp2[BUFLEN] = {'\0'};
	strcpy(temp, info.c_str());
	while (1)
	{

		// pentru ../
		char *p, *q = temp2, *s = temp;
		p = strstr(temp, "../");
		if (p == NULL)
			break;
		char *x = p;
		// ma duc la primul /
		x--;
		x--;
		while (x - temp >= 0)
		{
			if ((*x) == '/')
				break;
			x--;
		}

		if (x - temp < 0) break;
		// copiez pana la directorul ce ma intereseaza
		while (s != x)
		{
			(*q) = (*s);
			s++;
			q++;
		}
		s = p;
		s++;
		s++;
		// copiez restul
		while ((*s) != '\0')
		{
			(*q) = (*s);
			s++;
			q++;
		}
		(*q) = '\0';
		strcpy(temp, temp2);
	}

	// pentru ./
	// este analog
	while (1)
	{
		char *p, *q = temp2, *s = temp;
		p = strstr(temp, "./");
		if (p == NULL)
			break;

		while (s != p)
		{
			(*q) = (*s);
			s++;
			q++;
		}
		s++;
		s++;
		while ((*s) != '\0')
		{
			(*q) = (*s);
			s++;
			q++;
		}
		(*q) = '\0';
		strcpy(temp, temp2);
	}


	return string(temp);

}

void send_command(int client_socket)
{
	// trimit o comanda la un client nou

	char _temp[BUFLEN];
	string info =  links.top().first; // iau primul link
	int adancime_pagina = links.top().second; // adancimea corespunzatoare
	links.pop();// scot din coada

	if (info.at(info.size() - 1) == '/')
		info = info + indexhtml;

	string info2 = transforma(info);
	info = info2;
	// verific daca am descarcat resursa curenta
	// ca sa nu o mai descarc inca o data
	for (int i = 0 ; i < (int)downloaded_resources.size(); i++)
		if (downloaded_resources[i] == info)
		{
			sprintf(_temp, "Resursa %s a mai fost descarcata!\n", info.c_str());
			print_output(ERROR, (char *)_temp);
			return;
		}
	// verific daca resursa curenta nu este descarcata de un alt worker
	for (int i = 0; i < (int) in_download.size(); i++)
		if (in_download[i] == info)
		{
			sprintf(_temp, "Resursa %s este descarcata de alt client!\n", info.c_str());
			print_output(ERROR, (char *)_temp);
			return;
		}
	//cout << adancime_pagina << endl << flush;
	string file1 = take_last(info, 4);
	string file2 = take_last(info, 5);
	char file = 0;
	//cout << file1 << " " << file2 << endl << flush;
	if (file1 == ".html" || file2 == ".htm"  || file1 == ".htm" || file2 == ".html" )  {
		file = 1;
		if (recursiv == false && adancime_pagina > 0)
		{
			sprintf(_temp, "Suntem in mod nerecursiv, nu putem descarca %s\n", info.c_str());
			print_output(ERROR, (char *)_temp);
			return;
		}

		if (adancime_pagina > MAX_DEPTH) // daca adancimea e mai mare decat cea maxima
		{
			sprintf(_temp, "Resursa %s a atins adancimea maxima %d !\n", info.c_str(), MAX_DEPTH);
			print_output(ERROR, (char *)_temp);
			return;							// ma opresc
		}

	}


	if (file == 0 && !(file1[0] == '.' || file2[0] == '.'))
	{
		sprintf(_temp, "Cale/fisier incorect, nu putem descarca %s\n", info.c_str());
		print_output(ERROR, (char *)_temp);
		return;
	}
	if (file == 0 && everything == 0)
	{
		sprintf(_temp, "Trebuie sa descarcam doar pagini, nu putem descarca %s\n", info.c_str());
		print_output(ERROR, (char *)_temp);
		return;
	}

	char temp[BUFLEN] = {0};
	int msg_lenght = 0; // lungimea mesajului
	//[d][recursiv][everytihng][f / p][adancime_pagina][lungime - int][adresa]
	temp[0] = 'd'; msg_lenght++; // lesaj de tip descarcare
	temp[1] = (char)(recursiv); msg_lenght++; //daca este recursiv
	temp[2] = (char)(everything); msg_lenght++; // daca este in modul everything
	temp[3] = file; msg_lenght++; // daca este pagina sau nu
	temp[4] = adancime_pagina; msg_lenght++; // adancimea paginii
	int var_temp = info.size() + 1; // dimesiunea paginii

	memcpy(temp + 5, &(var_temp), sizeof(int)); msg_lenght += 4;
	strcpy(temp + 9, info.c_str()); msg_lenght += (info.size() + 1);

	sprintf(_temp, "Trmit clientului %d linkul %s\n", client_socket, info.c_str());
	print_output(STDOUT, _temp);


	int sended = sendInfo(client_socket, temp, msg_lenght);
	if (sended <= 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa trimit comanda la client!\n");
		return;
	}

	// eliberez din clienti
	clienti_liberi.pop();
	// introduc resursa in curs de descarcare
	in_download.push_back(info);
	commmand_in_progres = true;

}

void scrie_in_fisier(char path[], char mesaj[], int mesajlen)
{
	char _temp[BUFLEN];
	char http[10];
	strncpy(http, path, 7);
	// elimin http:// din fata daca exista
	if (strcmp(http, "http://") == 0)
		strcpy(path, path + 7);

	sprintf(_temp, "Calea unde descarc %s\n", path);
	print_output(STDOUT, (char *)_temp);
	// extrag numele si directorul unde sa descarc
	string nume = "";
	int i;
	for (i = strlen(path) - 1; i >= 0; i--)
	{
		if (path[i] == '/')
			break;
		nume = nume + path[i];
	}
	// numele fisierului este pe dos
	reverse(nume.begin(), nume.end());
	char director[BUFLEN] = {0};

	int j;
	for (j = 0 ; j <= i ; j++)
		director[j] = path[j];


	sprintf(_temp, "Numele fisierului: %s\n", nume.c_str());
	print_output(STDOUT, (char *)_temp);

	sprintf(_temp, "Numele directorului: %s\n", director);
	print_output(STDOUT, (char *)_temp);

	struct stat st;

	// verifica daca mai am ierahia de foldere specificata
	if (stat(director, &st) == -1) {

		char temp[BUFLEN];
		sprintf(temp, "mkdir -p %s", director);
		system(temp);
		print_output(STDOUT, (char *)"Am creeat directorul\n");
	}
	// scriu in fisier
	fstream fisierul_meu;
	fisierul_meu.open(path, ios::out | ios::app | ios::binary);
	fisierul_meu.write(mesaj, mesajlen);
	fisierul_meu.close();
}
void descarca(int client_socket, int len)
{

	char lungime_nume[sizeof(int)];
	// descarc lungimea fisierului (dimensiunea caii)
	int primit = Readline(client_socket, lungime_nume, sizeof(int));
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	len -= sizeof (int);
	int name_len;
	memcpy(&name_len, lungime_nume, sizeof(int));

	// descarc numele caii
	char path[name_len];
	primit =  Readline(client_socket, path, name_len);
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}

	// descarc mesajul efectiv
	len -= name_len;
	char mesaj[len + 2];
	if (len)
	{
		primit = Readline(client_socket, mesaj, len);
		if (primit < 0)
		{
			print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
			error("Not working");
		}

	}

	print_output(STDOUT, (char *)"Scriu in fisierul: ");
	print_output(STDOUT, path);
	print_output(STDOUT, (char *)"\n");

	scrie_in_fisier(path, mesaj, len);
}

void new_link(int client_socket, int len)
{
	// primesc de la client un nou link
	char adancime;
	// citesc adancimea
	int primit = recv(client_socket, &adancime, 1, 0);
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	len--;
	// aflu linkul catre resursa
	char link_to_resource[BUFLEN];
	primit = Readline(client_socket, link_to_resource, len);
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	// introduc noul link primit alaturi de resursa

	links.push(make_pair(string(link_to_resource), (int)adancime));
	char temp[BUFLEN];
	sprintf(temp, "Am primit urmatorul link: %s cu adancimea %d\n",
	        link_to_resource, (int)adancime);
	print_output(STDOUT, temp);
}


void add_resources(int  client_socket, int len)
{
	// am terminat de descarcat o resursa
	char lungime_nume[sizeof(int) + 1];
	// citesc lungimea linkului
	int primit = Readline(client_socket, lungime_nume, sizeof(int));
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	len -= sizeof (int);
	int name_len;
	memcpy(&name_len, lungime_nume, sizeof(int));
	// citesc calea respectiva
	char path[name_len + 2];
	primit =  Readline(client_socket, path, name_len);
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	// adaug in lista de descarcari
	downloaded_resources.push_back(string(path));

	char _temp[BUFLEN];
	sprintf(_temp, "Am terminat de descarcat: %s\n", path);
	print_output(STDOUT, _temp);
}

void receive_command(int client_socket)
{

	/*
	[tip_mesaj][lungime mesaj]
			pentru download fisier [lungime nume - int][nume-complet fisier][mesaj]
			pentru linkuri [adancime-pag][nume - complet link]
			pentru autentificare [lungime-ip][ip][lungime-port][port]

	*/


	// primesc comanda de la un clinet
	char tip_mesaj;
	int primit = recv(client_socket, &tip_mesaj, 1, 0);

	if (primit == 0) // inseamna ca am aun client care s-a deconectat
	{
		FD_CLR(client_socket, &read_fds); // nu mai ascult pe socketul sau
		closed_clients.insert(client_socket); // tin cont de faptul ca s-a inchis
		for (int i = 0 ; i < (int)clienti.size(); i++) // il elimin din lista de clienti
		{
			if (clienti[i].second == client_socket)
			{
				clienti.erase(clienti.begin() + i);
				break;
			}
		}

		return ;
	}

	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}

	// citesc lungimea unui mesaj
	char lungime_mesaj[sizeof(int) + 2];
	primit = Readline(client_socket, lungime_mesaj, sizeof(int));
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut citi de pe socket\n");
		error("Not working");
	}
	int len;
	memcpy(&len, lungime_mesaj, sizeof(int));

	len -= sizeof(int);
	// vad ce tip de mesaj este
	if (tip_mesaj == DESCARCARE)
		descarca(client_socket, len);
	else if (tip_mesaj == FREE)
	{
		clienti_liberi.push(client_socket);
		char _temp[BUFLEN];
		sprintf(_temp, "clientul %d este liber", client_socket);
		print_output(STDOUT, _temp);
	}
	else if (tip_mesaj == LINK_NOU)
	{
		new_link(client_socket, len);
	}
	else if (tip_mesaj == RESURSA_DESCARCATA)
	{
		add_resources(client_socket, len);
	}


}

int main(int argc, char *argv[])
{
	// determin argumentele cu care ruleaza programul
	determina_argumete(argc, argv);
	if (is_port ==  false)
	{
		cout << "Portul nu este precizat\nClosing...\n" << flush;
		return 0;
	}

	cout << "Serverul ruleaza: \n";
	cout << "Recursiv " << recursiv << "\n";
	cout << "Everything " << everything << "\n";
	cout << "Output " << fisier_log << "\n";
	cout << "Port " << port << endl << flush;


	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");


	memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;	// foloseste adresa IP a masinii
	serv_addr.sin_port = htons(port);

	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
		error("ERROR on binding");

	listen(sockfd, MAX_CLIENTS);

	//adaugam noul file descriptor (socketul pe care se asculta conexiuni) in multimea read_fds
	FD_SET(sockfd, &read_fds);

	fdmax = sockfd;
	char buffer[BUFLEN] = {'\0'};
	int i;

	cout << "Asteptam workeri \n";
	while (clienti.size() < MIN_CLIENTS)
	{

		tmp_fds = read_fds;
		if  (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("ERROR in select");

		if (FD_ISSET(sockfd, &tmp_fds)) // daca am conexiune
			new_conection();

		if (string(buffer) ==  "exit")
		{
			close(sockfd);
			if (is_outputlog)
			{
				f.close();
				g.close();
			}

			return 0;
		}
	}
	cout << "S-au conectat " << clienti.size() << " clieniti" << endl << flush;
	FD_SET(0, &read_fds);
	while (1)
	{
		if (closed_server == true)
			break;
		tmp_fds = read_fds;
		if  (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("ERROR in select");

		// ascult pe portul de stdin
		if (FD_ISSET(0, &tmp_fds)) {
			cin.getline(buffer, BUFLEN);
			analizeaza_comanda(buffer);
		}
		// ascult pe fiecare port
		for (i = 1; i <= fdmax; i++)
		{
			if (FD_ISSET(i, &tmp_fds)) // daca am conexiune
			{
				if ( i == sockfd)
					new_conection();
				else
					receive_command(i);
			}
		}
		// trmit cate o comanda la fiecare client
		if (clienti_liberi.size() == clienti.size())
			while (!clienti_liberi.empty() && links.size())
			{
				int client_free =  clienti_liberi.front();
				// verific sa nu se fi inchis
				bool is_in_closed = closed_clients.find(client_free) != closed_clients.end();
				if (is_in_closed)
				{
					clienti_liberi.pop();
					continue;
				}
				send_command(client_free);
			}
		// verifica daca am terminat de downloadat
		if (clienti_liberi.size() == clienti.size() && links.size() == 0 && commmand_in_progres)
		{
			print_output(STDOUT, (char *)"Am terminat TOT de downloadat \n");
			break;
		}

		if (commmand_in_progres == false && closed_server == true)
		{
			break;
		}


	}
	// spunce ce linkuri am descarcat
	for (int i = 0 ; i < (int) downloaded_resources.size(); i++)
	{
		print_output(STDOUT, (char *)"Am descarcat: ");
		print_output(STDOUT, (char *)downloaded_resources[i].c_str());
		print_output(STDOUT, (char*)"\n");
	}

	if (closed_server == false)
		close_server();

	close(sockfd);
	// inchid fisierele de log
	if (is_outputlog)
	{
		f.close();
		g.close();
	}

	return 0;
}
