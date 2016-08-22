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
#define BUFFLEN 50000
#define MAX_CITIT 40000
#define DESCARCARE 1
#define FREE 2
#define LINK_NOU 3
#define RESURSA_DESCARCATA 4

using namespace std;


struct sockaddr_in serv_addr;
struct sockaddr_in servHTTP;
int socketHTTP;


bool recursiv, everything, output, is_page;
bool is_port, is_outputlog;
bool liber;
bool adresa_server;
bool legatura_initiata;
bool linkuri_de_trimis;
bool closed;

bool first_req;

char download_file[BUFFLEN];
char server_ip[MAXLEN];
char host[BUFFLEN], resource[MAXLEN];

int pid = getpid(), sockfd;
int adancime_pagina;
int port, fdmax;

fstream f, g;
string fisier_log;
string fisier_log_stdout;
string fisier_log_stderr;

fd_set read_fds;	//multimea de citire folosita in select()
fd_set tmp_fds;	//multime folosita temporar
queue<string> links;

char get_command[MAXLEN] = "GET %s HTTP/1.0\r\n\r\n"; // comanada de request
char prev_msg[BUFFLEN];
int prev_l;

string pagina = "";

void print_output(int out, char mesaj[]) // 1 pentru stdout 0 pt sdt err
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

// trimit catre serv dim octeti ce se afla in vectorul info
// si returnez numarul de octeti cititi
int sendInfo(int serv, char * info, int dim)
{
	char c, *buffer;
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

// citesc pe socketul socfd maxlen octeti in vectorul vptr
// si returnez numarul de octeti cititi
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
			return -2;
		}
		(*buffer) = c;
		buffer++;
	}
	return i;
}

// ii trmit serverului un link nou
void afla_linknou(char message[], int & lungime_mesaj)
{
	int i;
	// ce link in trimit
	string _link_to_send = links.front();
	links.pop();

	char temp[BUFFLEN] = {'\0'};
	char cale[BUFFLEN] = {'\0'};

	strcpy(temp, download_file);

	if (_link_to_send.at(0) == '/') // linkul curent trimite catre rootul site-ului
	{
		string _temp;
		_temp = string(host) + _link_to_send;
		strcpy(cale, _temp.c_str());
	}
	else
	{
		// in celalalt caz, linkul trimite catre o adresa relativa
		// si determin intai calea
		for (i = strlen(temp) - 1 ; i >= 0 ; i--)
		{
			if (temp[i] == '/')
				break;
		}
		strncpy(cale, temp, i + 1);
		cale[i + 1] = '\0';
		// concatenez informatiile pentru a obtine un link bun
		strcat(cale, _link_to_send.c_str());
	}

	// trimit inforatia serverului, respectand protocolul

	char adancime_crt = adancime_pagina + 1; // adancimea paginii
	lungime_mesaj = sizeof(int) + sizeof(char) + strlen(cale) + 1; // lungimea mesajului
	memcpy(message + 1, &lungime_mesaj, sizeof(int));
	memcpy(message + 5, &adancime_crt, sizeof(char));
	memcpy(message + 6, cale, strlen(cale) + 1 ); // copiez calea efectiva

	char _temp[BUFFLEN];
	sprintf(_temp, "Trimit serverului linkul de descarcat: %s\n", cale);
	print_output(STDOUT, _temp);
}

void send_to_server(int info)
{
	// trimit la server informatii specifice
	char message [BUFFLEN];
	message[0] = info; // codul mesajului
	int lungime_mesaj = 0;

	if (info ==  RESURSA_DESCARCATA) // daca am terminat de descarcat o resursa
	{

		int lungime_adresa = strlen(download_file) + 1; // lungimea caii
		lungime_mesaj = sizeof(int) + sizeof(int) + lungime_adresa; // dimesiunea mesajului
		memcpy(message + 1, &lungime_mesaj, sizeof(int)); // lungimea mesajui
		memcpy(message + 5, &lungime_adresa, sizeof(int)); // lungimea adresei
		memcpy(message + 9, download_file, lungime_adresa ); // lungimea fisierului de download

	}
	if (info == LINK_NOU) // daca vreau sa ii trimit un link nou
	{
		afla_linknou(message, lungime_mesaj);
	}

	if (info == FREE) // daca vreau sa ii zic serverului ca sunt liber
	{
		lungime_mesaj =  sizeof(int);
	}
	// trimit mesajul propriu zis la server
	int sent =  sendInfo(sockfd, message, lungime_mesaj + 1);
	if (sent < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa trimit\n");
		return ;
	}

}



// functie ce transforma un numar in string
string number_to_string(long long  nr)
{
	char number[33];
	sprintf(number, "%lld", nr);
	string s(number);
	return s;
}

void error(const char *msg)
{
	perror(msg);
	exit(1);
}



// aflu portul serverului
void alfa_port(char port_string[])
{
	port = 0;
	int i;
	// formez numarul
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
// determin fisierul de log
void alfa_fisierlog(char fisier[])
{
	fisier_log = string(fisier);
	is_outputlog = true;
}
// determin fisierele cu care este rulat serverul
void determina_argumete(int argc, char *argv[])
{
	int i;
	is_port =  false;
	is_outputlog = false;
	adresa_server = false;
	fisier_log = "";

	for (i = 1; i < argc; i++)
	{

		if (!strcmp(argv[i], "-o")) // urmeaza fisier de output
			if (i + 1 < argc)
			{
				alfa_fisierlog(argv[i + 1]);
				i++;
				continue;
			}
		if (!strcmp(argv[i], "-p")) { // urmeaza portul serverului
			if (i + 1 < argc)
			{
				alfa_port(argv[i + 1]);
				i++;
				continue;
			}
		}
		if (!strcmp(argv[i], "-a")) { // urmeaza adresa serverului
			if (i + 1 < argc)
			{
				adresa_server = true;
				strcpy(server_ip, argv[i + 1]);
				i++;
				continue;
			}
		}
	}
	// daca am fisiere de log , atunci le creez
	if (fisier_log != "")
	{
		fisier_log_stdout = fisier_log + "_" + number_to_string(pid) + ".stdout";
		fisier_log_stderr = fisier_log + "_" + number_to_string(pid) + ".stderr";
		f.open(fisier_log_stdout.c_str(), ios::out); // deschid fisierele
		g.open(fisier_log_stderr.c_str(), ios::out);
	}

}

void receive_command(int server)
{
	//[d][recursiv][everytihng][f / p][adancime_pagina][lungime - int][adresa]
	// am primit o comanda de la server si o analizez
	char command;
	int primit;
	char _temp[BUFFLEN];
	primit = recv(server, &command, 1, 0);

	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa primesc\n");
		error("Not working");
	}

	if (command == 'e')
	{
		print_output(STDOUT, (char *)"Serverul se inchide. Astept eventualele descarcari\n" );
		closed =  true;
		return;
	}

	if (command != 'd')
	{
		print_output(ERROR, (char *)"Comanda necunoscuta\n" );
		return ;
	}
	sprintf(_temp, "Comanda server: %c\n", command);
	print_output(STDOUT, _temp);

	char header[4];
	primit = Readline(server, header, sizeof(int));
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa primesc\n");
		error("Not working");
	}
	recursiv = header[0]; // daca trebuie sa descarc in mod recursiv
	everything = header[1]; // daca trebuie sa fie everything
	is_page =  header[2]; // daca e pagina sau fisier
	adancime_pagina = header[3]; // adancime

	// lungimea caii
	char lungime[sizeof(int) + 1 ];
	primit = Readline(server, lungime, sizeof(int));
	if (primit < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa primesc\n");
		error("Not working");
	}
	int len;
	memcpy(&len, lungime, sizeof(int));

	// acum iau adresa, linkul
	char address[len + 2];
	primit = Readline(server, address, len);
	if (primit < 0)
	{
		error("Not working");
	}

	char http[10];
	strncpy(http, address, 7);
	// daca are http in fata, sau https il elimit
	if (strcmp(http, "http://") == 0) {
		strcpy(address, address + 7);
	}
	else
	{
		strncpy(http, address, 8);
		if (strcmp(http, "https://") == 0) {
			strcpy(address, address + 8);
		}

	}
	// descarc acest fisier:
	download_file[0] = '\0';
	strcpy(download_file, address);
	sprintf(_temp, "Trebuie sa descarc fisierul: %s\n", download_file);
	print_output(STDOUT, _temp);
	liber = false;
}
// daca am un fisier de descarcat, trebuie sa fac intai request la server
void initiaza_request()
{
	char _temp[BUFFLEN];
	int i;
	string site = ""; // determin site-ul
	for (i = 0; i < (int)strlen(download_file); i++)
		if (download_file[i] == '/')
			break;
		else
			site = site + download_file[i];

	strcpy(host, site.c_str()); // www.ceva.com

	strcpy(resource, download_file + i); // foo/bar/foo.html
	char comm[BUFFLEN];
	sprintf(comm, get_command, resource);


	struct hostent *host_site = gethostbyname(host);
	if (!host_site) {

		sprintf(_temp, "Siteul %s nu este bun\n", host);
		print_output(ERROR, _temp);
		send_to_server(FREE);
		return ;
	}

	if (host_site->h_addr_list[0] == NULL)
	{
		sprintf(_temp, "Ceva nu e ok cu ip-ul siteului %s \n", host);
		print_output(ERROR, _temp);
		send_to_server(FREE);
		return ;
	}
	// termint adresa ip
	char ip_address[MAXLEN];
	strcpy(ip_address, inet_ntoa( *( struct in_addr*)( host_site -> h_addr_list[0])));
	// creez socket
	socketHTTP = socket(AF_INET, SOCK_STREAM, 0);
	if (socketHTTP < 0)
	{
		print_output(ERROR, (char *)"Eroare la creearea socketului\n");
		send_to_server(FREE);
		return ;
	}

	memset(&servHTTP, 0, sizeof(servHTTP));
	servHTTP.sin_family = AF_INET;
	servHTTP.sin_port = htons(HTTP_PORT);


	if (inet_aton(ip_address, &servHTTP.sin_addr) <= 0 )
	{
		sprintf(_temp, "Adresa ip %s este invalida \n", ip_address);
		print_output(ERROR, _temp);
		send_to_server(FREE);
		return ;
	}

	if (connect(socketHTTP, (struct sockaddr *) &servHTTP, sizeof(servHTTP)) < 0 )
	{
		print_output(ERROR, (char *)"Eroare la conectare\n");
		send_to_server(FREE);
		return ;
	}

	int sended =  send(socketHTTP, comm, strlen(comm), 0);
	if (sended < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa trimit\n");
		send_to_server(FREE);
		return ;
	}

	sprintf(_temp, "Am trimis comanda catre %s pentru a descarca %s, si astetept raspunsul\n", host, resource);
	print_output(STDOUT, _temp);

	legatura_initiata = true;
	// citesc pe acest socket
	FD_SET(socketHTTP, &read_fds);
	if (socketHTTP >= fdmax)
		fdmax = socketHTTP;
}
// ii dau serverului o bucata de informatie descarcata (de lungime len)
void send_to_server_download(char buffer[], int len)
{
	char message [BUFFLEN] = {'\0'};
	message[0] = DESCARCARE; // mesajul este de tip descarcare
	int lungime_mesaj = 0;
	int lungime_adresa = strlen(download_file) + 1;// lungimea linkului de descarre
	lungime_mesaj = sizeof(int) + sizeof(int) + lungime_adresa + len; // lungimea toatala a mesajului
	memcpy(message + 1, &lungime_mesaj, sizeof(int)); // lungimea intregului mesaj
	memcpy(message + 5, &lungime_adresa, sizeof(int)); // lungimea caii, adreseis
	memcpy(message + 9, download_file, lungime_adresa ); // calea
	memcpy(message + 9 + lungime_adresa, buffer, len); // informatia trimisa

	int sent =  sendInfo(sockfd, message, lungime_mesaj + 1);
	if (sent < 0)
	{
		print_output(ERROR, (char *)"Nu am putut sa trimit la server\n");
		return ;
	}

	char _temp[BUFFLEN];
	sprintf(_temp, "Serverul descarca o bucata de %d octeti din fisierul %s\n", len, download_file);
	print_output(STDOUT, _temp);
}

// elimin headerul de raspuns din mesaj
void remove_header(char response[], int &rev)
{
	// citesc linie cu linie din raspuns
	// pana cand dau de o linie cu un singur enter
	char temp[BUFFLEN + 2];
	memcpy(temp, response, rev);
	stringstream sin(temp);
	char lin[BUFFLEN];
	int dim = 0; // cat am citit pana la momentul curent
	while (1)
	{
		sin.getline(lin, BUFFLEN - 1); // citesc o linie
		dim +=  (strlen(lin) + 1); // cresc dimensiunea
		// daca linia e goala sau dam dat doar de un singur enter (cariege return in cazul meu)
		if (strlen(lin) == 0 || (strlen (lin) == 1 && lin[0] == '\r'))
		{
			break;
		}
	}
	// raspunul va avea o dimesine mai mica
	rev -= dim;
	// copiez stringul corespunzator
	memcpy(response, temp + dim, rev);
	// am realizat primul request
	first_req = true;
}

// analizez daca linkul gasit este valid
void analizeaza(char legatura[])
{
	char *p;
	p = strstr(legatura, "://"); // ma duce catre alta resursa
	if (p !=  NULL)
		return;

	p = strstr(legatura, "#"); // ma duce catre alta resursa
	if (p !=  NULL)
		return;

	if (strncmp(legatura, "ftp://", 6) == 0)
		return ;
	if (strncmp(legatura, "http://", 7) == 0)
		return ;
	if (legatura[0] == '#')
		return ;

	if (strncmp(legatura, "https://", 8) == 0)
		return ;
	if (strncmp(legatura, "mailto:", 7) == 0)
		return ;

	if (strncmp(legatura, "file:", 5) == 0)
		return ;

	links.push(string(legatura));
	char _temp[BUFFLEN];
	sprintf(_temp, "Am parsat urmatorul link: %s\n", legatura);
	print_output(STDOUT, _temp);
}
// parsez (caut linkuri in raspunsul serverului)
void parse_response2()
{

	int dim = pagina.size();
	char temp [dim + 2];
	char _temp [dim + 2];
	int len;
	temp[0] = '\0';
	strcpy(temp, pagina.c_str());
	char *p = temp; // adresa de start
	char _find[dim + 2];
	char *q;
	char *w;
	while (1)
	{
		p = strstr(p, "<a"); // am gasit un href

		if (p == NULL)
			break;
		w = strstr(p, "/a>");

		if (w == NULL)
			break;

		len = 0;
		char *r, *unde_am_ramas;
		r = p;
		unde_am_ramas = w + 3;
		while (r != w)
		{
			_temp[len++] = (*r);
			r++;
		}
		_temp[len] = '\0';

		p = strstr(_temp, "href");
		if (p == NULL )
		{
			p = unde_am_ramas;
			continue;
		}
		p += 4; // trec mai departe
		// caut pana la ghilimele
		while (p != NULL && (*p) != '\0'  && (*p) != '"')
			p++;

		if (p == NULL || (*p) == '\0')
			break;
		p++; // trec de ghilimele
		if (p == NULL || (*p) == '\0')
			break;
		q = _find; // memorez linkul aici
		// caut pana la urmatoarele ghilimele
		while (p != NULL && (*p) != '\0' && (*p) != '"')
		{
			*q = *p;
			p++;
			q++;
		}

		if (p == NULL || (*p) == '\0')
			break;
		// gata linkul
		(*q) = '\0';

		analizeaza(_find);

		if (p == NULL || (*p) == '\0')
			break;
		p = unde_am_ramas;
	}
}


void parse_response()
{

	int dim = pagina.size();
	char temp [dim + 2];
	temp[0] = '\0';
	strcpy(temp, pagina.c_str());
	char *p = temp; // adresa de start
	char _find[dim + 2];
	char *q;
	while (1)
	{

		p = strstr(p, "href");
		if (p == NULL )
		{
			break;
		}
		char *x = p;
		while (x - temp >= 0)
		{
			if ((*x) == '<')
				break;
			x--;

		}
		if (x - temp < 0)
			break;
		x++;
		if ((*x) != 'a')
		{
			p += 4; // trec mai departe
			continue;
		}

		p += 4; // trec mai departe
		// caut pana la ghilimele
		while (p != NULL && (*p) != '\0'  && (*p) != '"')
			p++;

		if (p == NULL || (*p) == '\0')
			break;
		p++; // trec de ghilimele
		if (p == NULL || (*p) == '\0')
			break;
		q = _find; // memorez linkul aici
		// caut pana la urmatoarele ghilimele
		while (p != NULL && (*p) != '\0' && (*p) != '"')
		{
			*q = *p;
			p++;
			q++;
		}

		if (p == NULL || (*p) == '\0')
			break;
		// gata linkul
		(*q) = '\0';

		analizeaza(_find);

		if (p == NULL || (*p) == '\0')
			break;
	}
}
// incerc sa downladez
void download()
{
	if (legatura_initiata == false)
	{
		// facem request la server
		initiaza_request();
	}
	else
	{
		char response[MAX_CITIT + 3] = {'\0'};
		int rev = recv(socketHTTP, response, MAX_CITIT,  0);

		if (first_req ==  false) // daca e primul mesaj pe care l-am primit
		{
			// atunci stergem headerul de raspuns
			response[rev + 1] = '\0';
			remove_header(response, rev);

		}
		if (rev < 0)
		{
			print_output(STDOUT, (char*)"Nu am primit de la serverul HTTP\n");
			return ;
		}

		if (rev == 0)
		{
			char _temp[BUFFLEN];
			sprintf(_temp, "Am terminat de downloadat %s\n", download_file);
			print_output(STDOUT, _temp);
			liber = true; // clientul este acum liber si resetez toate informatiile
			first_req = false; // primul request
			legatura_initiata = false;
			prev_msg[0] = '\0';
			FD_CLR(socketHTTP, &read_fds); // nu mai ascult socketul de http
			send_to_server(RESURSA_DESCARCATA); // trimit serverului ca am terminat
			close(socketHTTP); // inchid socketul

			return ;
		}
		// trimit o bucata la server
		send_to_server_download(response, rev);
		// memorez pana la pozita curenta
		response[rev + 1] = '\0';
		pagina +=  response;
	}
}

int main(int argc, char *argv[])
{
	determina_argumete(argc, argv);

	if (is_port ==  false || adresa_server == false)
	{
		cout << "Port sau adresa server nespeicifcata!\n" << flush;
		return 0;
	}

	cout << "Clientul ruleaza: \n";
	cout << "Output " << fisier_log << "\n";
	cout << "Port " << port << endl << flush;
	cout << "Adresa server " << server_ip << endl << flush;

	// deschid socket nou
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	inet_aton(server_ip, &serv_addr.sin_addr);

	// ma conectez
	if (connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR connecting");



	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);
	FD_SET(sockfd, &read_fds);

	fdmax = sockfd;
	liber = true;
	while (1) {

		tmp_fds = read_fds;
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("ERROR in select");

		if (FD_ISSET(sockfd, &tmp_fds)) // am primit comanda de la server
			receive_command(sockfd);

		if (liber ==  false) // daca nu sunt liber atunci descarc
			download();


		if (liber == true) // daca sunt liber trimit linkurile parsate
		{
			// parsez
			parse_response();
			while (!links.empty())
			{
				send_to_server(LINK_NOU);
			}
			pagina = "";
			send_to_server(FREE);
			prev_msg[0] = '\0';
			prev_l = 0;
			if (closed)
			{
				//send_to_server(FREE);
				break;
			}

		}
	}

	close(sockfd);


	if (fisier_log != "")
	{
		f.close();
		g.close();
	}
	return 0;
}


