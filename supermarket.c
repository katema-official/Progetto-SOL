#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#define DEBUG 0

#define MAX_LENGTH_BUFFER 256
#define true 1
#define false 0		//per usare i booleani in modo comprensibile
#define factor_ms 1000000L

void* thread_cassiere(void* arg);
void* thread_cliente(void* arg);
void* thread_direttore(void* arg);

void Pthread_create(
	pthread_t *thread_id,
	const pthread_attr_t *attr,
	void* (*start_fcn) (void *),
	void* arg
);

void Pthread_join(
	pthread_t thread_id,
	void** status_ptr
);

void Pthread_exit(
	void* retval
);

void Pthread_mutex_lock(
	pthread_mutex_t *mutex
);

void Pthread_mutex_unlock(
	pthread_mutex_t *mutex
);

void Pthread_mutex_init(
	pthread_mutex_t * mtx,
	const pthread_mutexattr_t * attr
);

void Pthread_cond_signal(
	pthread_cond_t *cond
);

void Pthread_cond_broadcast(
	pthread_cond_t *cond
);

void Pthread_cond_wait(
	pthread_cond_t *cond,
	pthread_mutex_t * mtx
);

void Pthread_cond_init(
	pthread_cond_t * cnd,
	const pthread_condattr_t * attr
);

void Pthread_cancel(
	pthread_t tid
);

void Pthread_testcancel();


typedef struct configinfo{		//STRUCT generale, che contiene le informazioni di tutto il supermercato (clienti, cassiere, direttore...)
	int K;						//massimo numero di casse aperte
	int C;						//massimo numero di clienti che possono essere presenti contemporaneamente nel supermercato
	int E;						//massimo numero di clienti assenti (se numClienti<=C-E, fanne entrare altri E)
	int T;						//massimo tempo (in ms) che un cliente impiega a girare tra gli scaffali (minimo 10)
	int P;						//massimo numero di prodotti che un cliente può acquistare (minimo 0)
	int S;						//dopo quanto tempo (in ms) un cliente si guarda intorno per decidere se cambiare o meno coda (non considerato nella versione semplificata)
	int S1;						//parametro del direttore. Massimo numero di casse che possono avere 0 o 1 cliente in coda
	int S2;						//parametro del direttore. Massimo numero di clienti che possono stare in una coda
	int V;						//tempo (esatto) che un cassiere impiega per scannerizzare un prodotto alla cassa
	int D;						//dopo quanto tempo (in ms) un cassiere aggiorna il direttore della sua situazione (numero di clienti in coda)
	int TempoApertura;			//per quanto tempo (in s) una cassa deve rimanere aperta
	int CasseAperteInit;		//numero di casse aperte all'apertura del supermercato
	char FileDiLog[MAX_LENGTH_BUFFER];				//nome del file di log su cui scrivere il sunto dell'esecuzione
}configinfo;

typedef struct clienteinfo{
	int T;						//il cliente deve sapere quanto tempo al massimo può passare nel supermercato
	int P;						//il cliente deve sapere quanti prodotti al massimo può acquistare
	int V;						//il cliente deve sapere quanto tempo impiega un cassiere a scannerizzare un prodotto
	int K;						//il cliente deve sapere tra quante casse può scegliere
	int C;						//il cliente deve sapere quanti clienti possono esserci al massimo nel supermercato, così da usare tale valore come modulo per l'id
	int id_c;					//il cliente deve sapere il suo id % C
}clienteinfo;

typedef struct coda_cassa{
//	int id_cassa;				//la cassa in cui il cliente si mette in coda
	int prodotti_cliente;		//il numero di prodotti che il cliente ha acquistato
	int id_thread;				// l'id (da 0 a C-1) del thread che si è messo in coda
	struct coda_cassa *next;	//puntatore all'elemento della coda che si trova davanti (il cliente precedente)
}coda_cassa;

typedef struct cc_tempi_attesa{		//struct da cui costruirò gli elementi delle code riguardanti, per ogni cassa, il tempo di servizio di ogni cliente servito.
	long tempo_di_attesa;			//il tempo di attesa in coda per quel particolare cliente
	struct cc_tempi_attesa *next;	//puntatore all'elemento successivo della coda
}cc_tempi_attesa;

typedef struct direttoreinfo{
	int S1;
	int S2;
	int C;
	int E;
	int K;
	int D;
	int TempoApertura;
	int CasseAperteInit;
}direttoreinfo;

typedef struct clienti_attesa_direttore{
	int id_cliente;
	struct clienti_attesa_direttore *next;
}clienti_attesa_direttore;

typedef int bool;				//per semplicità di lettura


static FILE* file_log_clienti = NULL;
static FILE* file_log_cassieri = NULL;

//+++++++++++++++++++++++++++++++++++++++++++++++VARIABILI GLOBALI DA ACCEDERE IN MUTUA ESCLUSIONE++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//variabili generali, id cliente, numero di clienti in attesa di un cenno dal direttore e array dei cassieri aperti modificabile dal direttore
static int prodotti_acquistati_totale=0;								//numero totale di prodotti acquistati nella giornata
static int clienti_serviti_totale=0;									//numero totale di clienti serviti
static int id_cliente=0;												//id del cliente che entra nel supermercato, diverso per ogni cliente
static int clienti_con_zero_prodotti_in_attesa=0;						//numero di clienti che non hanno fatto acquisti e stanno aspettando il permesso per uscire
static bool* array_cassieri_aperti=NULL;								//array che mi dice, per ogni cassa, se questa è aperta o meno

//variabili di interesse dei clienti
static int* array_cassieri_numero_clienti_in_coda=NULL;					//array che mi dice, per ogni cassa, quanti clienti ha in coda. Verrà aggiornato dai clienti
//static int id_cliente_presente_nel_supermercato = 0;					//variabile globale che mi permette di identificare univocamente un cliente nel supermercato
static int* array_id_cliente_presente_nel_supermercato = NULL;			/*array utile per monitorare la situazione dei clienti presenti nel supermercato. Ogni posizione, relativa
																		ad un cliente, potrà avere 6 valori distinti:
																		-2 = il cliente è uscito dal supermercato ed è stata fatta la join su di lui (non occupa memoria)
																		-1 = il cliente è uscito dal supermercato/sta uscendo, ma non è ancora stata liberata la sua memoria
																		0 = il cliente ancora non è in una coda.
																		1 = il cliente è in coda ma non è il prossimo ad essere servito.
																		2 = il cliente è il prossimo ad essere servito (rispetto alla coda in cui si trova).
																		3 = Il cliente si era messo in coda ad una cassa, ma è stata chiusa e ne sta cercando un'altra.*/
																		
//variabili di interesse dei cassieri (e in parte anche dei clienti)
static coda_cassa** array_tails=NULL;									//array di puntatori alle code delle casse
static int* array_cassieri_numero_clienti_in_coda_direttore = NULL;		//array che mi dice, per ogni cassa, quanti clienti ha in coda. Verrà aggiornato solo dai cassieri ogni D tempo
static cc_tempi_attesa** array_tails_tda = NULL;						//array dove ogni cella punta a una coda che mi dice, per ogni cassa, il tempo di attesa in coda dei clienti
																		//che l'hanno frequentata
//le seguenti non hanno bisogno della mutex.
static int* array_TSC = NULL;											//array contenente, per ogni cassiere, il suo Tempo di Servizio Costante. Non sono previste lock per questa
																		//struttura dati che verrà solo letta
static int* array_cassieri_numero_prodotti_elaborati_totale = NULL;		//array che mi dice, per ogni cassa, il numero di prodotti totale che ha elaborato
static int* array_cassieri_numero_clienti_serviti_totale = NULL;		//array che mi dice, per ogni cassa, il numero di clienti totale che ha servito
static int* array_cassieri_numero_di_chiusure_totale = NULL;			//array che mi dice, per ogni cassa, il numero di chiusure effettuate in totale
static long* array_cassieri_tempo_di_apertura_totale = NULL;			//array che conserva il tempo totale di apertura di ogni singola cassa (in nanosecondi)
//non è presente un array id cassa in quanto il direttore, al momento della creazione del thread cassiere, gli passerà come argomento il suo id (da 0 a K-1)

//variabile che serve ai clienti con p=0
static clienti_attesa_direttore* direttore_tail = NULL;

//variabile che permette ai clienti di uscire subito qualora il supermercato stia chiudendo a causa di un segnale di sighup
static bool si_chiude_sigquit = false;

//variabile che permette di segnalare un cambiamento nella situazione generale del supermercato osservata dal direttore
static bool notifica_direttore_cambiamento = false;

//intero che entra in gioco quando il supermercato chiude a causa di una sighup. Ogni cassiere, dopo aver capito che è tempo di chiusura, aumenterà di uno il valore di questa
//variabile non appena avrà finito di servire ogni cliente (ci sarà quindi un comportameto speciale per il cliente in questo caso, dove serve i clienti senza aspettarsi che il
//direttore gli dica chiudere, non prima almeno di aver servito tutti).
static int sighup_numero_casse_concluse = 0;

//variabile booleana che permette, dopo l'arrivo di un sighup, di informare il direttore che un cliente è uscito. Serve a evitare attesa attiva
static bool uscito_un_cliente_dal_supermercato = false;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++MUTEX E CV PER LE RISORSE CONDIVISE (variabili globali)++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//mutex generali, id cliente, numero di clienti in attesa di un cenno dal direttore e array dei cassieri aperti modificabile dal direttore
static pthread_mutex_t MUTEX_prodotti_acquistati_totale = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t MUTEX_clienti_serviti_totale = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t MUTEX_id_cliente = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t MUTEX_clienti_con_zero_prodotti_in_attesa = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t* MUTEX_array_cassieri_aperti = NULL;

//mutex di interesse dei clienti
static pthread_mutex_t* MUTEX_array_cassieri_numero_clienti_in_coda = NULL;
static pthread_cond_t* CV_array_cassieri_numero_clienti_in_coda = NULL;									//CV relativa alla MUTEX soprastante
static pthread_mutex_t MUTEX_array_id_cliente_presente_nel_supermercato = PTHREAD_MUTEX_INITIALIZER;

//mutex e cv di interesse dei cassieri (e in parte anche dei clienti)
static pthread_mutex_t* MUTEX_array_tails = NULL;
static pthread_cond_t* CV_array_tails = NULL;
static pthread_mutex_t* MUTEX_array_cassieri_numero_clienti_in_coda_direttore = NULL;
static pthread_mutex_t* MUTEX_array_tails_tda = NULL;

//mutex relativa ai clienti con 0 prodotti che attendono il permesso del direttore per uscire
static pthread_mutex_t MUTEX_direttore_tail = PTHREAD_MUTEX_INITIALIZER;

//mutex per assicurarmi che al più un cliente scriva nel file di log clienti alla volta, per evitare conflitti
static pthread_mutex_t MUTEX_scrittura_clienti = PTHREAD_MUTEX_INITIALIZER;

//mutex per garantire che l'accesso alla variabile sia atomico
static pthread_mutex_t MUTEX_si_chiude_sigquit = PTHREAD_MUTEX_INITIALIZER;

//mutex che serve a racchiudere una wait e una signal utili per evitare che il direttore faccia attesa attiva
static pthread_mutex_t MUTEX_notifica_direttore_cambiamento = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CV_notifica_direttore_cambiamento = PTHREAD_COND_INITIALIZER;

//mutex e cv che permettono l'uso della variabile stessa
static pthread_mutex_t MUTEX_sighup_numero_casse_concluse = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CV_sighup_numero_casse_concluse = PTHREAD_COND_INITIALIZER;


//mutex e cv utili nella gestione degli ultimi clienti in caso di sighup, che permettono di evitare l'attesa attiva del direttore
static pthread_mutex_t MUTEX_uscito_un_cliente_dal_supermercato = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CV_uscito_un_cliente_dal_supermercato = PTHREAD_COND_INITIALIZER;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++FUNZIONI+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//funzione per inizializzare la struct che conterrà i valori del file di configurazione
configinfo* set_up_config_struct(configinfo* ci, FILE* fd);

//funzione per generare un numero casuale in un intervallo
int genera_random_in_intervallo(int lower, int upper, unsigned int s);

//funzione per l'inserimento di un valore nella coda di una cassa
void insert(int numero_cassa, int t_id, int t_prodotti);

//funzione per la cancellazione di un valore nella coda di una cassa
int delete(int id_cassa);

//funzione per la chiusura di una cassa, ovvero serve ad informare tutti i clienti in coda che la cassa è stata chiusa
void close_cashier(int id_cassa);

//funzione per l'inserimento nella coda dei tempi di attesa dei singoli clienti di un nuovo elemento (ovvero del tempo di attesa di un nuovo cliente, relativamente a una cassa)
void insert_time_waiting_client(long tempo, int cassa_servito);

//funzione per la conversione dei millisecondi in secondi con 3 cifre decimali
char* time_to_string(long time, char* ret);

//funzione che misura (in MILLISECONDI) una distanza temporale tra due istanti di tempo
long misura_distanza_temporale(struct timespec a, struct timespec b);

//funzione per calcolare, data la coda di una cassa, la somma dei tempi di attesa dei clienti che l'hanno frequentata
long calcola_attesa(cc_tempi_attesa* coda);

//funzione per inserire un cliente con 0 prodotti nella coda di attesa direttore
void insert_director(int id_cli);

//funzione usata dal direttore per far uscire i clienti con 0 prodotti acquistati
void delete_director();

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



//creo la struct su cui dovrò copiare i dati del file di configurazione
static configinfo* config_parameters = NULL;

//variabili globali per la gestione dei segnali SIGQUIT e SIGHUP
volatile sig_atomic_t sigquit_arrivato = 0;
volatile sig_atomic_t sighup_arrivato = 0;

//dichiaro i relativi handler ai segnali di cui sopra
void handler_sigquit();
void handler_sighup();

//MAIN
int main(int argc, char* argv[]){

	srand(time(0));
	
	struct sigaction sa_sigquit = {0};
	struct sigaction sa_sighup = {0};
	sa_sigquit.sa_handler = handler_sigquit;
	sa_sighup.sa_handler = handler_sighup;
	sigaction(SIGQUIT, &sa_sigquit, NULL);									//installo il gestore per SIGQUIT
	sigaction(SIGHUP, &sa_sighup, NULL);									//installo il gestore per SIGHUP
	
	
	if((file_log_clienti = fopen("log_clienti.txt", "w"))==NULL){
		printf("Errore nell'apertura del file di log dei clienti\n");
		exit(EXIT_FAILURE);
	}	
	if((file_log_cassieri = fopen("log_cassieri.txt", "w"))==NULL){
		printf("Errore nell'apertura del file di log dei cassieri\n");
		exit(EXIT_FAILURE);
	}
	
//++++++++++++++++++++++++++++++++++++INIZIALIZZAZIONE STRUTTURA GLOBALE DEL SUPERMERCATO++++++++++++++++++++++++++++++++++++

	//controllo che sia stato inserito un argomento
	if(argc!=2){
		printf("Errore: bisogna passare come argomento il file config.txt\n");
		exit(EXIT_FAILURE);
	}
	
	//controllo che l'argomento inserito abbia il nome del file di configurazione
	char nomefile[MAX_LENGTH_BUFFER];
	strcpy(nomefile,argv[argc-1]);
	if(strcmp(nomefile,"config.txt")!=0){
		printf("Errore: bisogna passare come argomento il file config.txt\n");
		exit(EXIT_FAILURE);
	}
	
	//apro il file passato come argomento in sola lettura
	FILE* configuration_file = NULL;
	if((configuration_file=fopen(nomefile, "r"))==NULL){
		printf("Errore: il file di configurazione non è stato aperto");
		exit(EXIT_FAILURE);
	}
	
	//inizializzo la struttura
	config_parameters = (configinfo*)malloc(sizeof(configinfo));
	if(config_parameters==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	
	if((config_parameters=set_up_config_struct(config_parameters, configuration_file))==NULL){
		printf("Struttura di configurazione non inizializzata. Terminazione programma\n");
		exit(EXIT_FAILURE);
	}else{
		fclose(configuration_file);
	}
	
//++++++++++++++++++++++++++++++++++++INIZIALIZZAZIONE STRUTTURE DATI DEI CASSIERI++++++++++++++++++++++++++++++++++++
	
	//a questo punto so il numero di cassieri e possono inizializzare i loro array (quello che mi dice chi è aperto e quello che viene aggiornato dai clienti)
	array_cassieri_aperti = malloc((config_parameters->K)*sizeof(bool));
	if(array_cassieri_aperti==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	array_cassieri_numero_clienti_in_coda = malloc((config_parameters->K)*sizeof(int));
	if(array_cassieri_numero_clienti_in_coda==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	
	//inizializzo col numero adeguato di cassieri anche l'array di mutex cassieri aperti. Così, atomicamente, potrò sapere se una cassa è aperta o chiusa
	//inizializzo col numero adeguato di cassieri l'array del numero di clienti in coda (mutex). Così, atomicamente, potrò aggiornare il numero di clienti presenti in una coda
	//inizializzo col numero adeguato di cassieri l'array del numero di clienti in coda (cond). Così, un cassiere potrà sospendersi sulla sua CV.
	MUTEX_array_cassieri_aperti = (pthread_mutex_t*)malloc(config_parameters->K*sizeof(pthread_mutex_t));
	if(MUTEX_array_cassieri_aperti==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	MUTEX_array_cassieri_numero_clienti_in_coda = (pthread_mutex_t*)malloc(config_parameters->K*sizeof(pthread_mutex_t));
	if(MUTEX_array_cassieri_numero_clienti_in_coda==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	CV_array_cassieri_numero_clienti_in_coda = (pthread_cond_t*)malloc(config_parameters->K*sizeof(pthread_cond_t));
	if(CV_array_cassieri_numero_clienti_in_coda==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		Pthread_mutex_init(&MUTEX_array_cassieri_aperti[i], NULL);
		Pthread_mutex_init(&MUTEX_array_cassieri_numero_clienti_in_coda[i], NULL);
		Pthread_cond_init(&CV_array_cassieri_numero_clienti_in_coda[i], NULL);
	}
	
	//poi li inizializzo
	for(int i=0;i<config_parameters->K; i++){
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);
		Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
		array_cassieri_aperti[i]=false;											//tutti i cassieri sono chiusi										
		array_cassieri_numero_clienti_in_coda[i]=0;								//non ci sono clienti in coda a nesusna cassa
		Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);
	}
												
	
	
	//ora che so quante casse ci sono, posso creare l'array di MUTEX e di CV relative alle code delle casse (una MUTEX e una CV per cassa)
	MUTEX_array_tails = (pthread_mutex_t*)malloc(config_parameters->K*sizeof(pthread_mutex_t));
	if(MUTEX_array_tails==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	CV_array_tails = (pthread_cond_t*)malloc(config_parameters->K*sizeof(pthread_cond_t));
	if(CV_array_tails==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		Pthread_mutex_init(&MUTEX_array_tails[i], NULL);
		Pthread_cond_init(&CV_array_tails[i], NULL);
	}
	
	//inizializzo le code delle casse, che inizialmente puntano a NULL in quanto non c'è nessuno in coda ad una cassa. Ogni posizione dell'array corrisponde ad una cassa
	array_tails=(coda_cassa**)malloc(config_parameters->K*sizeof(coda_cassa*));	
	if(array_tails==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){									
		array_tails[i] = NULL;
	}
	
	//alloco e inizializzo l'array riguardante il tempo di servizio costante dei cassieri
	array_TSC = (int*)malloc(config_parameters->K*sizeof(int));
	if(array_TSC==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		array_TSC[i] = (rand() % 60) + 20;
	}
	
	//creo l'array di mutex relativo a array_tails_tda
	MUTEX_array_tails_tda = (pthread_mutex_t*)malloc(config_parameters->K*sizeof(pthread_mutex_t));
	if(MUTEX_array_tails_tda==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		Pthread_mutex_init(&MUTEX_array_tails_tda[i], NULL);
	}
	
	//inizializzo le code che, per ogni cassa, mi dicono il tempo di attesa di ogni cliente che è stato servito in quella cassa. Ogni posizione dell'array corrisponde ad una cassa
	array_tails_tda=(cc_tempi_attesa**)malloc(config_parameters->K*sizeof(cc_tempi_attesa*));
	if(array_tails_tda==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){									
		array_tails_tda[i] = NULL;
	}
	
	//creo e inizializzo gli array dei cassieri relativi a prodotti-clienti-chiusure totali, e quello per il tempo di apertura totale
	array_cassieri_numero_prodotti_elaborati_totale=(int*)malloc(config_parameters->K*sizeof(int));
	if(array_cassieri_numero_prodotti_elaborati_totale==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	array_cassieri_numero_clienti_serviti_totale=(int*)malloc(config_parameters->K*sizeof(int));
	if(array_cassieri_numero_clienti_serviti_totale==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	array_cassieri_numero_di_chiusure_totale=(int*)malloc(config_parameters->K*sizeof(int));
	if(array_cassieri_numero_di_chiusure_totale==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	array_cassieri_tempo_di_apertura_totale=(long*)malloc(config_parameters->K*sizeof(long));
	if(array_cassieri_tempo_di_apertura_totale==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		array_cassieri_numero_prodotti_elaborati_totale[i] = 0;
		array_cassieri_numero_clienti_serviti_totale[i] = 0;
		array_cassieri_numero_di_chiusure_totale[i] = 0;
		array_cassieri_tempo_di_apertura_totale[i] = 0L;
	}
	
//++++++++++++++++++++++++++++++++++++INIZIALIZZAZIONE STRUTTURE DATI DEI CLIENTI++++++++++++++++++++++++++++++++++++

	//a questo punto so il numero massimo di clienti e posso creare il suo array (relativo a quelli attualmente presenti nel supermercato)
	array_id_cliente_presente_nel_supermercato = malloc((config_parameters->C)*sizeof(int));
	if(array_id_cliente_presente_nel_supermercato==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	
	//poi lo inizializzo (inizialmente nessuno è in coda e nessuno occupa memoria, quindi tutto a -2)
	Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	for(int i=0;i<config_parameters->C; i++){
		array_id_cliente_presente_nel_supermercato[i] = -2;
	}
	Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	
//++++++++++++++++++++++++++++++++++++INIZIALIZZAZIONE STRUTTURE DATI DEL DIRETTORE++++++++++++++++++++++++++++++++++++

	//inizializzo l'array che contiene le mutex del numero di clienti in coda ad ogni cassa, aggiornato dai cassieri ogni D millisecondi e sul quale il direttore prendere delle decisioni
	MUTEX_array_cassieri_numero_clienti_in_coda_direttore = (pthread_mutex_t*)malloc(config_parameters->K*sizeof(pthread_mutex_t));
	if(MUTEX_array_cassieri_numero_clienti_in_coda_direttore==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		Pthread_mutex_init(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[i], NULL);
	}
	
	//inizializzo anche l'array vero e proprio relativo alla mutex qui sopra
	array_cassieri_numero_clienti_in_coda_direttore = (int*)malloc(config_parameters->K*sizeof(int));
	if(array_cassieri_numero_clienti_in_coda_direttore==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i=0; i<config_parameters->K; i++){
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[i]);
		array_cassieri_numero_clienti_in_coda_direttore[i] = 0;
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[i]);
	}
	
//----------------------------------------------CREAZIONE DIRETTORE----------------------------------------------
	
	if(DEBUG) fprintf(stderr, "INIZIO DIRETTORE\n");
	
	pthread_t direttore;
	Pthread_create(&direttore, NULL, &thread_direttore, NULL);
	Pthread_join(direttore, NULL);
	
	if(DEBUG) fprintf(stderr, "FINE DIRETTORE\n");

	
	long* tempi_attesa_totale = (long*)malloc((config_parameters->K)*sizeof(long));
	if(tempi_attesa_totale==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i = 0; i < config_parameters->K; i++){
		Pthread_mutex_lock(&MUTEX_array_tails_tda[i]);
		tempi_attesa_totale[i] = calcola_attesa(array_tails_tda[i]);
		Pthread_mutex_unlock(&MUTEX_array_tails_tda[i]);
	}
	
	//vado a scrivere i risultati dei cassieri in un file di log
	char* tmp = NULL;
	char* tmp2 = NULL;
	for(int i=0; i<config_parameters->K; i++){
		tmp = (char*)malloc(6*sizeof(char));
		if(tmp==NULL){
			printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
			exit(EXIT_FAILURE);
		}
		tmp = time_to_string(array_cassieri_tempo_di_apertura_totale[i], tmp);
		tmp2 = (char*)malloc(6*sizeof(char));
		if(tmp2==NULL){
			printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
			exit(EXIT_FAILURE);
		}
		if(array_cassieri_numero_clienti_serviti_totale[i] == 0){
			tmp2 = time_to_string(000000000L, tmp2);
		}else{
			tmp2 = time_to_string(tempi_attesa_totale[i]/(long)array_cassieri_numero_clienti_serviti_totale[i], tmp2);
		}
		fprintf(file_log_cassieri, "| id cassa = %d | n. prodotti elaborati = %d | n. di clienti = %d | tempo tot. di apertura = %s | tempo medio di servizio = %s | n. di chiusure = %d |\n",
		i,
		array_cassieri_numero_prodotti_elaborati_totale[i],
		array_cassieri_numero_clienti_serviti_totale[i],
		tmp,
		tmp2,
		array_cassieri_numero_di_chiusure_totale[i]);
		
		free(tmp);
		free(tmp2);
	}
	
	//libero la memoria allocata dinamicamente
	free(array_cassieri_aperti);
	free(array_cassieri_numero_clienti_in_coda);
	free(MUTEX_array_cassieri_aperti);
	free(MUTEX_array_cassieri_numero_clienti_in_coda);
	free(CV_array_cassieri_numero_clienti_in_coda);
	free(MUTEX_array_tails);
	free(CV_array_tails);
	free(array_tails);
	free(array_TSC);
	free(MUTEX_array_tails_tda);
	free(array_tails_tda);
	free(array_cassieri_numero_prodotti_elaborati_totale);
	free(array_cassieri_numero_clienti_serviti_totale);
	free(array_cassieri_numero_di_chiusure_totale);
	free(array_cassieri_tempo_di_apertura_totale);
	free(array_id_cliente_presente_nel_supermercato);
	free(MUTEX_array_cassieri_numero_clienti_in_coda_direttore);
	free(array_cassieri_numero_clienti_in_coda_direttore);
	free(tempi_attesa_totale);
	
	fclose(file_log_clienti);									//chiudo i due file di log per i cassieri e i clienti e li riapro in lettura
	fclose(file_log_cassieri);
	if((file_log_clienti = fopen("log_clienti.txt", "r"))==NULL){
		printf("Errore nell'apertura del file di log dei clienti\n");
		exit(EXIT_FAILURE);
	}			
	if((file_log_cassieri = fopen("log_cassieri.txt", "r"))==NULL){
		printf("Errore nell'apertura del file di log dei cassieri\n");
		exit(EXIT_FAILURE);
	}
	FILE* file_di_log = NULL;
	if((file_di_log = fopen(config_parameters->FileDiLog, "w"))==NULL){
		printf("Errore nell'apertura del file di log\n");
		exit(EXIT_FAILURE);
	}
	
	char c;
	c=fgetc(file_log_clienti);
	while(c!=EOF){
		fputc(c, file_di_log);
		c = fgetc(file_log_clienti);
	}
	
	c=fgetc(file_log_cassieri);
	while(c!=EOF){
		fputc(c, file_di_log);
		c = fgetc(file_log_cassieri);
	}
	
	fclose(file_log_clienti);
	fclose(file_log_cassieri);
	fclose(file_di_log);
	
	free(config_parameters);
	
	if(DEBUG) fprintf(stderr, "Il supermercato ha ufficialmente finito!\n");
	
	//free(config_parameters->FileDiLog);
	
	return 0;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



/*-------INIZIO FUNZIONI PER LA GESTIONE DEGLI ERRORI-------*/

//pthread_create sicura con controllo del valore di ritorno
void Pthread_create(
	pthread_t *thread_id,
	const pthread_attr_t *attr,
	void* (*start_fcn) (void *),
	void* arg
){
	int ret = pthread_create(thread_id, attr, start_fcn, arg);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_create = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}


//pthread_join sicura con controllo del valore di ritorno
void Pthread_join(
	pthread_t thread_id,
	void** status_ptr
){
	int ret = pthread_join(thread_id, status_ptr);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_join = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}


/*pthread_exit "sicura". La pthread_exit non ha un valore di ritorno, pertanto
non servirebbe una sua versione sicura, ma per evitare confusione e aumentare
il grado di coerenze la ri-definiamo con la P maiuscola.*/
void Pthread_exit(
	void* retval
){
	pthread_exit(retval);
}


//MUTEX
//pthread_mutex_lock sicura
void Pthread_mutex_lock(
	pthread_mutex_t *mutex
){
	int ret = pthread_mutex_lock(mutex);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_mutex_lock = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//pthread_mutex_unlock sicura
void Pthread_mutex_unlock(
	pthread_mutex_t *mutex
){
	int ret = pthread_mutex_unlock(mutex);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_mutex_unlock = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

/*pthread_mutex_init sicura. Serve a inizializzare una mutex non globale.
Ritorna sempre 0, ma di nuovo, per migliorare la leggibilità, ri-scriviamo anche
questa con la P maiuscola*/
void Pthread_mutex_init(
	pthread_mutex_t * mtx,
	const pthread_mutexattr_t * attr
){
	int ret = pthread_mutex_init(mtx, attr);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_mutex_init = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//CV
//pthread_cond_signal sicura
void Pthread_cond_signal(
	pthread_cond_t *cond
){
	int ret = pthread_cond_signal(cond);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_cond_signal = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//pthread_cond_broadcast sicura
void Pthread_cond_broadcast(
	pthread_cond_t *cond
){
	int ret = pthread_cond_broadcast(cond);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_cond_broadcast = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//pthread_cond_wait sicura
void Pthread_cond_wait(
	pthread_cond_t *cond,
	pthread_mutex_t * mtx
){
	int ret = pthread_cond_wait(cond, mtx);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_cond_wait = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//pthread_cond_init sicura. Serve a inizializzare una cv non globale.
void Pthread_cond_init(
	pthread_cond_t * cnd,
	const pthread_condattr_t * attr
){
	int ret = pthread_cond_init(cnd, attr);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_cond_init = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}

//pthread_cancel sicura
void Pthread_cancel(
	pthread_t tid
){
	int ret = pthread_cancel(tid);
	if(ret!=0){
		printf("Errore alla linea %d della pthread_cancel = %d\n", __LINE__, ret);
		exit(EXIT_FAILURE);
	}
}


//pthread_testcancel "sicura"
void Pthread_testcancel(){
	pthread_testcancel();
}

/*-------FINE FUNZIONI PER LA GESTIONE DEGLI ERRORI-------*/


/*La funzione prende in ingresso la struct da inizializzare e il fd del file config.txt. Il file viene letto da fgets
e la struct viene inizializzata come si deve*/
configinfo* set_up_config_struct(configinfo* ci, FILE* fd){
	char config_buf[MAX_LENGTH_BUFFER];
	char current_value[MAX_LENGTH_BUFFER];
	
	int param_corr=0;												//il parametro corrente
	int i=0;														//posizione corrente nel file
	int j=0;														//indice del secondo buffer
	while((fgets(config_buf, MAX_LENGTH_BUFFER, fd)!=NULL)){		//leggo il file
		i=0;
		j=0;
		while(config_buf[i]!='='){									//mi muovo nel buffer fino a quando non ho trovato un uguale. Subito dopo avrò un valore.
			i++;
		}
		i++;														//mi sposto dopo l'uguale
		while(config_buf[i]!='\0'){									//faccio una copia del valore che dura fino a quando non ho raggiunto la fine della riga corrente
			current_value[j]=config_buf[i];
			i++;
			j++;
		}															
		current_value[j]='\0';										//inserisco il carattere di terminazione del valore corrente
		switch(param_corr){											//capisco quale parametro ho trovato
			case 0:
				ci->K=atoi(current_value);
				break;
			case 1:
				ci->C=atoi(current_value);
				break;
			case 2:
				ci->E=atoi(current_value);
				break;
			case 3:
				ci->T=atoi(current_value);
				break;
			case 4:
				ci->P=atoi(current_value);
				break;
			case 5:
				ci->S=atoi(current_value);
				break;
			case 6:
				ci->S1=atoi(current_value);
				break;
			case 7:
				ci->S2=atoi(current_value);
				break;
			case 8:
				ci->V=atoi(current_value);
				break;
			case 9:
				ci->D=atoi(current_value);
				break;
			case 10:
				ci->TempoApertura=atoi(current_value);
				break;
			case 11:	
				ci->CasseAperteInit=atoi(current_value);
				break;
			case 12:
				current_value[j-1]='\0';
				strcpy(ci->FileDiLog, current_value);
				break;
			default:
				break;
		}
		param_corr++;
		j=0;
	}
	return ci;
}

//funzione per la creazione di valori casuali in un intervallo di numeri
int genera_random_in_intervallo(int lower, int upper, unsigned int s){
	int num = (rand_r(&s) % (upper - lower + 1)) + lower; 
	return num;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ FUNZIONE THREAD CLIENTE ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

void* thread_cliente(void* arg){
	
	if(DEBUG) fprintf(stderr, "Generato il cliente con id (modulo K) = %d\n", *((int*) arg));
	//precondizione: array_id_cliente_del_supermercato[*((int*) arg)] deve essere messo a 0 dal direttore nel momento in cui decide di fare entrare il cliente con questo id (quando
	//lo crea insomma). è invece il cliente a settare la variabile a -1 quando esce dal supermercato a seguito di alcuni acquisti
	
	//inizializzo gli attributi del cliente
	clienteinfo* struct_con_info = (clienteinfo*)malloc(sizeof(clienteinfo));
	if(struct_con_info==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	struct_con_info->T = config_parameters->T;
	struct_con_info->P = config_parameters->P;
	struct_con_info->V = config_parameters->V;
	struct_con_info->K = config_parameters->K;
	struct_con_info->C = config_parameters->C;
	struct_con_info->id_c = *((int*) arg);													//Deve essere il direttore a dire al cliente il suo id nel supermercato
																							//(% 50), o potrei avere problemi inerenti alla sovrascrittura di dati ancora rilevanti.
	
	//variabili "personali" del cliente
	unsigned int *mystate = arg;															//righe di codice che mi servono per generare casualmente il numero
	*mystate = time(NULL) ^ getpid() ^ pthread_self();										
	int p = genera_random_in_intervallo(0,config_parameters->P, *mystate);					//p è il numero di prodotti effettivi acquistati dal cliente
	int t = genera_random_in_intervallo(10,config_parameters->T, *mystate);					//t è il tempo effettivo che il cliente passa tra gli scaffali
	int cassa_scelta = -1;																	//il cliente non ha ancora scelto una cassa
	struct timespec inizio_attesa_in_coda, fine_attesa_in_coda; 							//variabili per tenere traccia del tempo speso in coda da un cliente
	int checkout = false;																	//diventa vera non appena il cliente può uscire dal supermercato
	int ho_scelto_una_cassa = false;														//diventerà true non appena il cliente avrà scelto la sua vera e propria coda
	int tmp_cassa_scelta = -1;																//mi serve per rilasciare correttamente la mutex di una cassa che è chiusa
	int numero_di_casse_visitate=0;
	bool served = true;																		//per sapere se il cliente va contato nel file di log (p>0) oppure no
	
	if(p==0){
		//aumento la variabile globale che mi dice il numero di clienti in attesa e aggiungo questo thread cassiere alla coda che il direttore deve gestire.
		Pthread_mutex_lock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
		Pthread_mutex_lock(&MUTEX_direttore_tail);
		insert_director(struct_con_info->id_c);
		clienti_con_zero_prodotti_in_attesa++;													//aumenta il numero di clienti in attesa
		Pthread_mutex_unlock(&MUTEX_direttore_tail);
		Pthread_mutex_unlock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
		served = false;
		free(struct_con_info);
	}
	
if(served==true){
	
	struct timespec t_nanoseconds;																//struct utili a contare i nanosecondi
	t_nanoseconds.tv_sec=0;
	t_nanoseconds.tv_nsec=(long)t*factor_ms;													//t millisecondi * 10^6
	nanosleep(&t_nanoseconds, NULL);															//il cliente trascorre t millisecondi nel supermercato
	
	clock_gettime(CLOCK_REALTIME, &inizio_attesa_in_coda);										//inizio tempo di attesa in coda
	
	while(checkout==false){//inizio while
		
		numero_di_casse_visitate++;																//se mi trovo qui, vuol dire che sto per visitare una nuova cassa
		
		//il cliente sceglie una cassa aperta
		cassa_scelta = genera_random_in_intervallo(0,config_parameters->K, *mystate);			//il cliente sceglie randomicamente una cassa in cui mettersi in attesa
		if(cassa_scelta==config_parameters->K){													
			cassa_scelta--;
		}
		if(DEBUG) fprintf(stderr, "cassa_scelta inizialmente dal thread %d = %d\n", struct_con_info->id_c, cassa_scelta);

		while(ho_scelto_una_cassa==false){													//il cliente non sa ancora se la cassa scelta casualmente sarà aperta. Pertanto, fino a che		
			Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[cassa_scelta]);						//non si è assicurato che tale cassa sia veramente aperta, rimane nel ciclo. Se la cassa 
			if(array_cassieri_aperti[cassa_scelta] == true){									//non è libera, fa la unlock della mutex di quella cassa. Altrimenti, si tiene stretta la
				ho_scelto_una_cassa = true;														//mutex fino a che non si è davvero messo in coda.
			}else{		
				tmp_cassa_scelta = cassa_scelta;		
				
				cassa_scelta = (cassa_scelta + 1) % config_parameters->K;								
			
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[tmp_cassa_scelta]);
				
				//se è arrivato un segnale di sigquit, tutte le casse sono chiuse. Pertanto il cliente deve rinunciare e uscire subito
				Pthread_mutex_lock(&MUTEX_si_chiude_sigquit);
				if(si_chiude_sigquit==true){
					Pthread_mutex_unlock(&MUTEX_si_chiude_sigquit);
					Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
					array_id_cliente_presente_nel_supermercato[struct_con_info->id_c] = -1;		//"libero" la cella dell'array che riguardava lo stato di questo cliente dato che
					Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);	//è stato costretto ad uscire
					if(DEBUG) fprintf(stderr, "Il cliente %d sta uscendo a seguito del SEGNALE SIGQUIT\n", struct_con_info->id_c);
					free(struct_con_info);
					return NULL;
				}
				Pthread_mutex_unlock(&MUTEX_si_chiude_sigquit);
			}
		}
		
		if(DEBUG) fprintf(stderr, "cassa_scelta davvero dal thread %d = %d\n", struct_con_info->id_c, cassa_scelta);
		
		//il cliente si mette in coda con la insert
		Pthread_mutex_lock(&MUTEX_array_tails[cassa_scelta]);									//sempre in mutua esclusione sulla cassa scelta (non posso rischiare di mettermi in coda
		insert(cassa_scelta, struct_con_info->id_c, p);											//in una cassa che potrebbe stare per chiudere), mi inserisco in coda. 
		
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[cassa_scelta]);			//aumento atomicamente il numero di clienti in coda a quella cassa. Ovvero notifico al cassiere
		array_cassieri_numero_clienti_in_coda[cassa_scelta]++;									//che mi sono messo in coda
		Pthread_cond_signal(&CV_array_cassieri_numero_clienti_in_coda[cassa_scelta]);			//informo il cassiere che qualcuno si è messo in coda
		
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[cassa_scelta]);		//rilascio in ordine inverso le tre mutex che avevo acquisito
		Pthread_mutex_unlock(&MUTEX_array_tails[cassa_scelta]);
		Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[cassa_scelta]);						//da questo momento in poi, un altro cliente potrà mettersi con successo in coda a questa cassa
		
		
		
		//devo controllare se sono il prossimo ad essere servito o meno. Per farlo devo avere la mutua esclusione al mio stato di attesa (relativo al mio id % 50)
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		while(array_id_cliente_presente_nel_supermercato[struct_con_info->id_c] == 1){			//il cliente, finché deve attendere il suo turno, si sospende sulla CV della sua coda
			Pthread_cond_wait(&CV_array_tails[cassa_scelta], &MUTEX_array_id_cliente_presente_nel_supermercato);
		}
		
		//se finalmente il cliente non è più sospeso, controllo cosa è successo.
		if(array_id_cliente_presente_nel_supermercato[struct_con_info->id_c]==2){				
			checkout = true;
			clock_gettime(CLOCK_REALTIME, &fine_attesa_in_coda);
			if(DEBUG) fprintf(stderr, "il cliente %d è stato servito e sta uscendo\n", struct_con_info->id_c);
		}else if(array_id_cliente_presente_nel_supermercato[struct_con_info->id_c]==3){
			//il ciclo ricomincia
			ho_scelto_una_cassa=false;
			tmp_cassa_scelta = -1;
			if(DEBUG) fprintf(stderr, "il cliente %d sta cambiando coda\n", struct_con_info->id_c);		
		}
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		
	}//fine while 
	
	

	long tempo_totale_speso_in_coda = misura_distanza_temporale(inizio_attesa_in_coda, fine_attesa_in_coda) + (long) ((array_TSC[cassa_scelta]+struct_con_info->V*p));	//in millisecondi
	long tempo_totale_speso_nel_supermercato = tempo_totale_speso_in_coda + (long) t;			//in millisecondi
	int my_true_id = -1;
	Pthread_mutex_lock(&MUTEX_id_cliente);
	my_true_id = id_cliente;																	//stabilisco il mio id univoco
	id_cliente++;																				//e lo aumento di uno così che il prossimo cliente ne abbia uno diverso
	Pthread_mutex_unlock(&MUTEX_id_cliente);				
	
	Pthread_mutex_lock(&MUTEX_array_tails_tda[cassa_scelta]);
	insert_time_waiting_client(tempo_totale_speso_in_coda, cassa_scelta);						//aggiungo il tempo speso in coda da me in questa cassa alla coda relativa in MILLISECONDI
	Pthread_mutex_unlock(&MUTEX_array_tails_tda[cassa_scelta]);
	
	Pthread_mutex_lock(&MUTEX_clienti_serviti_totale);											//aumento il numero di clienti serviti in totale durante la giornata
	clienti_serviti_totale++;
	Pthread_mutex_unlock(&MUTEX_clienti_serviti_totale);
	
	Pthread_mutex_lock(&MUTEX_prodotti_acquistati_totale);										//aumento il numero di prodotti acquistati in totale durante la giornata
	prodotti_acquistati_totale+=p;
	Pthread_mutex_unlock(&MUTEX_prodotti_acquistati_totale);
	
	char* ttsns_string = (char*)malloc(6*sizeof(char));
	if(ttsns_string==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	char* ttsic_string = (char*)malloc(6*sizeof(char));
	if(ttsic_string==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	ttsns_string = time_to_string(tempo_totale_speso_nel_supermercato, ttsns_string);
	ttsic_string = time_to_string(tempo_totale_speso_in_coda, ttsic_string);
	
	Pthread_mutex_lock(&MUTEX_scrittura_clienti);
	fprintf(file_log_clienti, "| id cliente = %d | n. prodotti acquistati = %d | tempo totale nel super. = %s | tempo totale speso in coda = %s | n. di code visitate = %d |\n",
	my_true_id,
	p,
	ttsns_string,
	ttsic_string,
	numero_di_casse_visitate);
	Pthread_mutex_unlock(&MUTEX_scrittura_clienti);
	if(DEBUG) fprintf(stderr, "IL CLIENTE %d (my_true_id = %d) HA FINITO DI SCRIVERE SUL FILE DI LOG DEI CLIENTI\n", struct_con_info->id_c, my_true_id);
	free(ttsns_string);
	free(ttsic_string);
	
	
	Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	array_id_cliente_presente_nel_supermercato[struct_con_info->id_c] = -1;				//"libero" la cella dell'array che riguardava lo stato di questo cliente dato che esce
	Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	
	
	free(struct_con_info);
	
}		//fine dell'"if served"
	
	//per informare il direttore, a sighup arrivato, che un cliente è uscito
	Pthread_mutex_lock(&MUTEX_uscito_un_cliente_dal_supermercato);
	if(sighup_arrivato==true){
		uscito_un_cliente_dal_supermercato=true;
		Pthread_cond_signal(&CV_uscito_un_cliente_dal_supermercato);
	}
	Pthread_mutex_unlock(&MUTEX_uscito_un_cliente_dal_supermercato);
	
	
	return NULL;
}



//funzione per l'inserimento di un cliente in una coda di una cassa.
void insert(int numero_cassa, int t_id, int t_prodotti){
	coda_cassa* new=(coda_cassa*)malloc(sizeof(coda_cassa));								//Sono sicuro che il cliente riuscirà a mettersi in coda dato che entro nella insert
	if(new==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	new->prodotti_cliente = t_prodotti;
	new->id_thread=t_id;																	//Aggiungo dunque un elemento alla coda. Le informazioni che mi interessa mantenere,
	new->next=NULL;																			//per ogni cliente in coda, sono: la cassa in cui è in coda, il suo id % C e quello davanti
	if(array_tails[numero_cassa]==NULL){													//Se non c'è nessuno davanti a me, allora verrò servito subito
		array_tails[numero_cassa]=new;
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		array_id_cliente_presente_nel_supermercato[new->id_thread] = 2;						//2=verrò servito subito
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	}else{																					//Se invece c'è qualcuno davanti a me, chi lo sa quando verrò servito
		new->next=array_tails[numero_cassa];												//Mi metto dopo quello prima di me (punto a lui)
		array_tails[numero_cassa]=new;														//Il puntatore alla fine della coda si sposta indietro di uno
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		array_id_cliente_presente_nel_supermercato[new->id_thread] = 1;						//1=devo aspettare che venga servito qualcuno prima di essere servito a mia volta
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);													
	}

}

void insert_time_waiting_client(long tempo, int cassa_servito){
	cc_tempi_attesa* new = (cc_tempi_attesa*)malloc(sizeof(cc_tempi_attesa));
	if(new==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	new->tempo_di_attesa = tempo;
	new->next=NULL;
	if(array_tails_tda[cassa_servito]==NULL){
		array_tails_tda[cassa_servito]=new;
	}else{
		new->next=array_tails_tda[cassa_servito];
		array_tails_tda[cassa_servito] = new;
	}
	
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ FUNZIONE THREAD CASSIERE ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void* thread_cassiere(void* arg){
	int my_id = *((int*) arg);													//l'id del cassiere risvegliato. L'argomento deve essergli passato dal direttore
	int prodotti_elaborati_current=0;											//numero di prodotti che ho elaborato in questa sessione di apertura. Andrà sommato al totale quando chiudo
	int clienti_serviti_current=0;												//numero di clienti che ho servito in questa sessione di apertura. Andrà sommato al totale quando chiudo.
	struct timespec inizio_sessione, fine_sessione;								//strutture dati per sapere il tempo di questa sessione di apertura
	clock_gettime(CLOCK_REALTIME, &inizio_sessione);							//ha inizio il tempo di questa sessione
	bool devo_chiudere = false;													//variabile che diventerà vera se, dopo aver servito il cliente, il cassiere dovrà chiudere
	
	struct timespec t1, t2;														//per sapere se devo mandare un aggiornamento all'array del direttore
	bool timer_passato = true;													//variabile che mi permetterà di mandare, ogni D millisecondi, un aggiornamento al direttore
	long D = (long)config_parameters->D;										//ogni quanti millisecondi mandare aggiornamenti al direttore
	int prodotti_scannerizzati_adesso=0;										//prodotti che un cliente ha comprato. Mi serve per capire di quanto farlo dormire
	struct timespec p_nanoseconds_cassiere;
	p_nanoseconds_cassiere.tv_sec=0;
	p_nanoseconds_cassiere.tv_nsec=0;
	
	if(DEBUG) fprintf(stderr, "Ha aperto il cassiere %d\n", my_id);
	
	//ciclo di vita del thread cassiere
	while(devo_chiudere==false){
	
		prodotti_scannerizzati_adesso=0;
		p_nanoseconds_cassiere.tv_sec=0;
		p_nanoseconds_cassiere.tv_nsec=0;
		
		if(timer_passato==true){												//se ho aggiornato l'array del direttore, allora ricomincio a contare i millisecondi D
			clock_gettime(CLOCK_REALTIME, &t1);
			timer_passato=false;
		}
		
		
		//quando il segnale di sighup è arrivato, devo servire tutti i clienti e poi informare il direttore del fatto che ho finito
		if((sighup_arrivato == 1) && (devo_chiudere == false)){															//se il sighup è arrivato e io sono aperta
			if(DEBUG) fprintf(stderr, "Il CASSIERE %d ha ricevuto il SIGHUP e sta servendo i clienti in modo diverso\n", my_id);
			bool ho_servito_tutti_i_clienti = false;
			while(ho_servito_tutti_i_clienti == false){
				Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
				if(array_cassieri_numero_clienti_in_coda[my_id] > 0){
					//devo accedere alla coda della cassa e rimuovere il primo elemento
					Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
					Pthread_mutex_lock(&MUTEX_array_tails[my_id]);							//accedo in mutua esclusione alla coda della mia cassa
					prodotti_scannerizzati_adesso = delete(my_id);							//i prodotti del cliente che vado a servire
					prodotti_elaborati_current = prodotti_elaborati_current + prodotti_scannerizzati_adesso;	//aumento il numero di prodotti elaborati con quelli del cliente appena servito
					Pthread_cond_broadcast(&CV_array_tails[my_id]);							//risveglio tutti i clienti, ma chi non ha la sua variabile aggiornata a 2 torna a dormire
					Pthread_mutex_unlock(&MUTEX_array_tails[my_id]);
					clienti_serviti_current++;
			
					p_nanoseconds_cassiere.tv_sec=0;
					p_nanoseconds_cassiere.tv_nsec=(long)((array_TSC[my_id]+config_parameters->V*prodotti_scannerizzati_adesso)*factor_ms);
					nanosleep(&p_nanoseconds_cassiere, NULL);
					if(DEBUG) fprintf(stderr, "Il cassiere %d ha servito un cliente\n", my_id);
				}else{
					Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
					//ho servito tutti i clienti, dunque posso avvisare il direttore di ciò
					ho_servito_tutti_i_clienti = true;
					Pthread_mutex_lock(&MUTEX_sighup_numero_casse_concluse);
					sighup_numero_casse_concluse++;
					Pthread_cond_signal(&CV_sighup_numero_casse_concluse);
					Pthread_mutex_unlock(&MUTEX_sighup_numero_casse_concluse);
					if(DEBUG) fprintf(stderr, "Il cassiere %d ha servito tutti i clienti\n", my_id);
				}
			}
			
		}
		
		//acquisisco in mutua esclusione la mutex relativa al numero di persone in coda alla mia cassa. Se non c'è nessuno, mi sospendo, aspettando un segnale.
		Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[my_id]);
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
		
		//mi sospendo fintanto che non ho clienti in coda e sono aperto
		while(array_cassieri_numero_clienti_in_coda[my_id]==0 && array_cassieri_aperti[my_id]==true){
			Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
			Pthread_cond_wait(&CV_array_cassieri_numero_clienti_in_coda[my_id], &MUTEX_array_cassieri_aperti[my_id]);
			Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
		}
		
		//quando mi risveglio, potrei essere stato risvegliato per due motivi:
		//1) il direttore ha visto che non ci sono clienti in coda, e pertanto ha deciso di chiudermi
		//2) un cliente si è aggiunto alla coda
		
		//se sono stato risvegliato perché avevo 0 clienti e devo chiudere setto la variabile devo_chiudere a true. Poi rilascio le lock.
		//settare a true questa variabile mi permette di decidere se devo servire qualcuno oppure no.
		//se non devo chiudere, allora procederò a servire il prossimo cliente.
		//se invece devo chiudere perché non ci sono abbastanza clienti, informero quelli ancora in coda che devono cambiare coda
		if(array_cassieri_aperti[my_id]==false){
			devo_chiudere = true;
		}
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
		Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[my_id]);
		
		//se sono stato risvegliato perché invece devo servire qualcuno, lo servo
		if(devo_chiudere==false){
			//devo accedere alla coda della cassa e rimuovere il primo elemento
			Pthread_mutex_lock(&MUTEX_array_tails[my_id]);							//accedo in mutua esclusione alla coda della mia cassa
			prodotti_scannerizzati_adesso = delete(my_id);							//i prodotti del cliente che vado a servire
			prodotti_elaborati_current = prodotti_elaborati_current + prodotti_scannerizzati_adesso;	//aumento il numero di prodotti elaborati con quelli del cliente appena servito
			Pthread_cond_broadcast(&CV_array_tails[my_id]);							//risveglio tutti i clienti, ma chi non ha la sua variabile aggiornata a 2 torna a dormire
			Pthread_mutex_unlock(&MUTEX_array_tails[my_id]);
			clienti_serviti_current++;
			
			//faccio una nanosleep che simula il tempo di servizio del cliente
			p_nanoseconds_cassiere.tv_sec=0;
			p_nanoseconds_cassiere.tv_nsec=(long)((array_TSC[my_id]+config_parameters->V*prodotti_scannerizzati_adesso)*factor_ms);
			nanosleep(&p_nanoseconds_cassiere, NULL);
			
			//dopo aver servito un cliente, controllo che il direttore non abbia deciso nel frattempo di farmi chiudere. Ma questo solo se il direttore non ha preso la decisione di farmi
			//chiudere PRIMA che io servissi il cliente.
			Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[my_id]);
			if(array_cassieri_aperti[my_id]==false){
				devo_chiudere = true;
			}
			Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[my_id]);
		
		
		}
		
		//se entro qui è perché il direttore mi ha detto di chiudere dato che ho 0 o 1 clienti in coda. Pertanto, devo dire agli eventuali clienti in coda, con una funzione,
		//di cambiare fila
		if(devo_chiudere==true){
			Pthread_mutex_lock(&MUTEX_array_tails[my_id]);
			close_cashier(my_id);													//setto a 3 il valore dei clienti in attesa
			Pthread_cond_broadcast(&CV_array_tails[my_id]);							//li informo che c'è stato un cambiamento (la cassa ha chiuso)
			Pthread_mutex_unlock(&MUTEX_array_tails[my_id]);
			if(DEBUG) fprintf(stderr, "Il cassiere %d ha capito che deve chiudere e lo sta facendo, informando i clienti\n", my_id);
		}
		
		
		clock_gettime(CLOCK_REALTIME, &t2);
		
		//controllo quanti millisecondi sono passati da quando ho servito questo cliente. Se ho superato D, allora aggiorno l'array del direttore, così che abbia una versione
		//più aggiornata del numero di clienti in coda alla cassa in esecuzione
		if((misura_distanza_temporale(t1, t2) >= D) && devo_chiudere==false){								
			Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
			Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[my_id]);
			array_cassieri_numero_clienti_in_coda_direttore[my_id] = array_cassieri_numero_clienti_in_coda[my_id];
			
			if(DEBUG) fprintf(stderr, "Aggiornamento dell'array_direttore dal cassiere %d con n. clienti in coda sua = %d\n", my_id, array_cassieri_numero_clienti_in_coda_direttore[my_id]);
			
			Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[my_id]);
			Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[my_id]);
			timer_passato = true;
			
			Pthread_mutex_lock(&MUTEX_notifica_direttore_cambiamento);		
			notifica_direttore_cambiamento = true;					
			Pthread_cond_signal(&CV_notifica_direttore_cambiamento);								//notifico il direttore del fatto che la situazione del supermercato è cambiata
			Pthread_mutex_unlock(&MUTEX_notifica_direttore_cambiamento);							//e che quindi potrebbe avere piacere nel darle un'occhiata
			
			
		}
		
		
	}
	
	clock_gettime(CLOCK_REALTIME, &fine_sessione);
	
	
	//aggiornamento della situazione "globale" della cassa
	array_cassieri_numero_prodotti_elaborati_totale[my_id] += prodotti_elaborati_current;
	array_cassieri_numero_clienti_serviti_totale[my_id] += clienti_serviti_current;
	array_cassieri_numero_di_chiusure_totale[my_id]++;
	long tempo_apertura_corrente = misura_distanza_temporale(inizio_sessione, fine_sessione);
	array_cassieri_tempo_di_apertura_totale[my_id] += tempo_apertura_corrente;
	if(DEBUG) fprintf(stderr, "Definitiva chiusura del cassiere %d\n", my_id);
	
	return NULL;
}

//funzione che, dato il numero di una cassa, rimuove l'elemento in testa. Assume che almeno un elemento ci sia. La funzione inoltre aggiornaarray_id_cliente_presente_nel_supermercato.
//restituisce il numero di prodotti che il cliente aveva con se.
int delete(int id_cassa){
	
	int ret = -1;
	coda_cassa* aux = NULL;				//dichiaro un puntatore ausiliario alla coda della cassa
	aux = array_tails[id_cassa];
	
	if(aux==NULL){
		fprintf(stderr, "Errore nella delete: nessun cliente è in coda!\n");
		exit(EXIT_FAILURE);
	}
	
	if((aux->next)==NULL){				//se c'è una sola persona in attesa alla cassa...
		array_tails[id_cassa]=NULL;		//...me la salvo in aux e rimuovo dalla coda il cliente, facendo puntare array_tails[id_cassa] a NULL
		if(DEBUG) fprintf(stderr, "Rimozione di un solo cliente alla cassa %d del thread cliente %d\n", id_cassa, aux->id_thread);
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
		array_cassieri_numero_clienti_in_coda[id_cassa]--;							//il cassiere aggiorna il numero di clienti presenti nella sua coda diminuendolo di uno
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
		ret = aux->prodotti_cliente;
	}else{									//altrimenti, se ci sono due o più clienti in coda, devo rimuovere il primo dalla coda
		
		while(aux->next!=NULL){				//scorro tutta la coda fino a quando non ho trovato l'elemento che sta in cima (il prossimo cliente che deve essere servito);
			aux=aux->next;
		}
		ret = aux->prodotti_cliente;		//mi salvo il numero di prodotti di questo cliente
		if(DEBUG) fprintf(stderr, "Rimozione di un cliente ma ce ne sono altri alla cassa %d del thread cliente %d\n", id_cassa, aux->id_thread);
		//dichiaro un altro puntatore ausiliario che mi permetterà di rimuovere dalla cassa il cliente che sta venendo servito (se c'è più di un cliente in coda)
		
		coda_cassa* aux2 = NULL;
		aux2 = array_tails[id_cassa];
		while(aux2->next != aux){
			aux2=aux2->next;
		}
		aux2->next = NULL;					//così ho rimosso dalla coda il cliente che stava in cima
		
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		if(array_id_cliente_presente_nel_supermercato[aux2->id_thread]!=-1 && array_id_cliente_presente_nel_supermercato[aux2->id_thread]!=-2){
			array_id_cliente_presente_nel_supermercato[aux2->id_thread]=2;				//questo è il prossimo cliente ad essere servito
		}
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		
		Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
		array_cassieri_numero_clienti_in_coda[id_cassa]--;							//il cassiere aggiorna il numero di clienti presenti nella sua coda diminuendolo di uno
		Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
	}
	
	free(aux);																		//libero la memoria del cliente servito
	
	return ret;
}

//con questa funzione informo tutti i clienti in coda ad una data cassa che la cassa sta chiudendo, e che pertanto devono cercarsi una nuova cassa
void close_cashier(int id_cassa){
	if(DEBUG) fprintf(stderr, "La cassa %d sta avvisando i clienti in coda che sta chiudendo\n", id_cassa);
	coda_cassa* aux = NULL;														//puntatore al cliente corrente
	coda_cassa* aux_da_liberare = NULL;
	Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);		//acquisisco la mutex dell'array delle condizioni dei clienti nel supermercato
	if(array_tails[id_cassa]!=NULL){
		aux = array_tails[id_cassa];											//se la coda della cassa ha ancora clienti, punto alla fine della coda
		array_tails[id_cassa] = NULL;											//tanto poi la coda dovrà essere libera, e ho già aux che punta alla coda
		while(aux!=NULL){
			if(array_id_cliente_presente_nel_supermercato[aux->id_thread]!=-1 && array_id_cliente_presente_nel_supermercato[aux->id_thread]!=-2){
				array_id_cliente_presente_nel_supermercato[aux->id_thread]=3;	//aggiorno, nell'array di condizione dei clienti globale, la condizione del cliente corrente, ma solo
			}																	//se è un cliente che non è ancora uscito (da qui il controllo !=-1 e !=-2
			aux_da_liberare = aux;												//mi salvo il cliente di cui mi sono occupato
			aux=aux->next;														//passo a quello successivo
			free(aux_da_liberare);												//libero quello di cui mi sono occupato
			aux_da_liberare=NULL;												//per sicurezza
		}
	}
	Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
	array_cassieri_numero_clienti_in_coda[id_cassa] = 0;						//essendo chiuso avrò sicuramente 0 clienti in coda
	Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[id_cassa]);
	Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
}


//funzione che, dato in input un char* allocato con una malloc(6*sizeof(char)), restituisce lo stesso char* contenente il tempo time in formato stringa con 3 cifre decimali
char* time_to_string(long time, char* ret){
	char container[32];
	sprintf(container, "%ld", time);
	int j=0;
	while(container[j]!='\0'){
		j++;
	}
	switch(j){
		case 0:
			strcpy(ret, "0.000");
		break;
		case 1:
			ret[4] = container[0];
			ret[3] = '0';
			ret[2] = '0';
			ret[1] = '.';
			ret[0] = '0';
		break;
		case 2:
			ret[4] = container[1];
			ret[3] = container[0];
			ret[2] = '0';
			ret[1] = '.';
			ret[0] = '0';
		break;
		case 3:
			ret[4] = container[2];
			ret[3] = container[1];
			ret[2] = container[0];
			ret[1] = '.';
			ret[0] = '0';
		break;
		case 4:
			ret[4] = container[3];
			ret[3] = container[2];
			ret[2] = container[1];
			ret[1] = '.';
			ret[0] = container[0];
		break;
		case 5:
			ret[4] = container[3];
			ret[3] = container[2];
			ret[2] = '.';
			ret[1] = container[1];
			ret[0] = container[0];
		break;
		case 6:
			ret[4] = container[3];
			ret[3] = '.';
			ret[2] = container[2];
			ret[1] = container[1];
			ret[0] = container[0];
		break;
		default:
		break;
	}
	ret[5] = '\0';
	return ret;
}


//funzione che, date in input due timespec, misura la distanza (in MILLISECONDI) tra i due istanti di tempo.
long misura_distanza_temporale(struct timespec a, struct timespec b){
	long secondi = (b.tv_sec - a.tv_sec) * 1000000000L;
	long nanosecondi = abs(b.tv_nsec - a.tv_nsec);
	long somma = secondi + nanosecondi;
	somma = somma / 1000000L;
	return somma;
}

//funzione che somma i tempi di attesa di tutti i clienti che sono stati serviti nella cassa "coda"
long calcola_attesa(cc_tempi_attesa* coda){
	long res=0;										//valore di ritorno inizializzato					
	cc_tempi_attesa* aux = NULL;					//servirà a liberare celle di memoria già analizzate
	while(coda!=NULL){								//scorro la coda
		res = res + coda->tempo_di_attesa;			//aggiungo al totale il tempo del cliente corrente
		aux = coda;									//mi salvo in aux il cliente appena analizzato
		coda = coda->next;							//passo al successivo
		free(aux);									//libero la memoria del cliente analizzato
		aux=NULL;									//per sicurezza
	}
	return res;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ FUNZIONE THREAD DIRETTORE ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

void* thread_direttore(void* arg){
	
	//inizializzo la struttura contenente le informazioni di rilievo per il direttore
	direttoreinfo* struct_con_info = (direttoreinfo*)malloc(sizeof(direttoreinfo));
	if(struct_con_info==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	struct_con_info->S1 = config_parameters->S1;
	struct_con_info->S2 = config_parameters->S2;
	struct_con_info->C = config_parameters->C;
	struct_con_info->E = config_parameters->E;
	struct_con_info->K = config_parameters->K;
	struct_con_info->D = config_parameters->D;
	struct_con_info->TempoApertura = config_parameters->TempoApertura;
	struct_con_info->CasseAperteInit = config_parameters->CasseAperteInit;
	
	//inizializzo un array di pthread_t che rappresenterà i cassieri nel supermercato
	pthread_t* array_thread_id_cassiere =(pthread_t*)malloc(struct_con_info->K*sizeof(pthread_t));
	if(array_thread_id_cassiere==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	
	//array per assicurarmi che l'id del cassiere generato sia quello giusto
	int* array_id_cassiere=(int*)malloc(struct_con_info->K*sizeof(int));
	if(array_id_cassiere==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	for(int i = 0; i<struct_con_info->K; i++){
		array_id_cassiere[i] = i;
	}
	
	//Apro il numero di casse iniziali (praticamente quelle aperte all'inizio della giornata) specificate nel file di configurazione
	for(int i = 0; i < struct_con_info->CasseAperteInit; i++){
		Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
		array_cassieri_aperti[i] = true;																//apro le casse iniziali
		Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
		Pthread_create(&array_thread_id_cassiere[i], NULL, &thread_cassiere, &array_id_cassiere[i]);	//creo le casse iniziali
	}
	
	
	//inizializzo un array di pthread_t che rappresenterà i clienti presenti nel supermercato
	pthread_t* array_thread_id_clienti =(pthread_t*)malloc(struct_con_info->C*sizeof(pthread_t));
	if(array_thread_id_clienti==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	
	//array per assicurarmi che l'id del cliente generato sia quello giusto
	int* array_id_clienti =(int*)malloc((struct_con_info->C)*sizeof(int));
	if(array_id_clienti==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	for(int i = 0; i < struct_con_info->C; i++){
		array_id_clienti[i] = i;
		array_id_cliente_presente_nel_supermercato[i] = 0;						//dato che subito dopo andrò a creare C clienti, voglio assicurarmi che la loro condizione sia 0
	}
	Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
	
	//vado a creare i C thread cliente (all'inizio voglio che ci siano C clienti)
	for(int i = 0; i < struct_con_info->C; i++){
		array_id_clienti[i] = i;
		Pthread_create(&array_thread_id_clienti[i], NULL, &thread_cliente, (void*) &array_id_clienti[i]);
		if(DEBUG) fprintf(stderr, "Creazione (iniziale) da parte del direttore del thread cliente %d\n", i);
	}
	
																	
	struct timespec t1, t2;											//ogni D millisecondi va a vedere la situazione
	bool azzera_contatore = true;
	int clienti_attuali = 0;
	int casse_aperte_attuali = 0;
	int casse_con_massimo_una_persona_in_coda = 0;
	bool ho_chiuso_una_cassa = false;
	int c = 0;
	bool esiste_una_cassa_con_S2_persone_in_coda = false;
	bool ho_aperto_una_cassa = false;
	
	while(sigquit_arrivato==0 && sighup_arrivato==0){						//lavoro fino a che non è il momento di chiudere
		
		if(DEBUG) fprintf(stderr, "INIZIO CICLO WHILE DIRETTORE\n");
		
		if(azzera_contatore==true){
			clock_gettime(CLOCK_REALTIME, &t1);										//inizio a contare i D millisecondi
			azzera_contatore = false;
		}
		
		clienti_attuali = 0;
		casse_aperte_attuali = 0;
		casse_con_massimo_una_persona_in_coda = 0;
		ho_chiuso_una_cassa = false;
		c = 0;
		esiste_una_cassa_con_S2_persone_in_coda = false;
		ho_aperto_una_cassa = false;
		
		//come prima cosa, controllo se ci sono clienti in attesa di uscire.
		Pthread_mutex_lock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
		if(clienti_con_zero_prodotti_in_attesa > 0){								//se i clienti in attesa del direttore esistono 
			Pthread_mutex_lock(&MUTEX_direttore_tail);
			if(DEBUG) fprintf(stderr, "Il direttore deve rimuovere almeno un cliente con ZERO PRODOTTI\n");
			delete_director();
			clienti_con_zero_prodotti_in_attesa=0;
			Pthread_mutex_unlock(&MUTEX_direttore_tail);
		}
		Pthread_mutex_unlock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
		
		
		//adesso vado a scandire l'array_id_cliente_presente_nel_supermercato e a contare quante celle sono a -1 (ovvero quanti clienti sono assenti)
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		for(int i = 0; i < struct_con_info->C; i++){								//devo contare i clienti nel supermercato
			if((array_id_cliente_presente_nel_supermercato[i] != -1) && (array_id_cliente_presente_nel_supermercato[i] != -2)){
				clienti_attuali++;
			}
		}
		if(DEBUG) fprintf(stderr, "Il direttore ha rilevato che nel supermercato sono attualmente presenti %d clienti\n", clienti_attuali);
		if(clienti_attuali <= (struct_con_info->C - struct_con_info->E)){			//se i clienti attuali sono meno di C-E (tolleranza)
			for(int i = 0; i < struct_con_info->C; i++){							//cerco gli id con valore -1 (cliente non presente nel supermercato)		
				if(array_id_cliente_presente_nel_supermercato[i] == -1){			//trovato il cliente assente
					array_id_clienti[i] = i;
					
					Pthread_join(array_thread_id_clienti[i], NULL);					//il thread che sto aspettando (che è un cliente) o è già uscito e quindi la join ritorna subito,
																					//oppure gli manca poco all'uscita.
					array_id_cliente_presente_nel_supermercato[i] = -2;
					array_id_cliente_presente_nel_supermercato[i] = 0;				//con questo numero segnalo che il cliente di posizione i nell'array è entrato
					array_id_clienti[i] = i;
					Pthread_create(&array_thread_id_clienti[i], NULL, &thread_cliente, &array_id_clienti[i]);		//creo tale cliente (lo faccio effettivamente entrare)
					if(DEBUG) fprintf(stderr, "Creazione (nuova) da parte del direttore del thread cliente i = %d\n", i);
				}
			}
		
		}
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		
		clock_gettime(CLOCK_REALTIME, &t2);											//finisco di contare i millisecondi
		
		//se i D millisecondi sono passati, controlla il numero di clienti in coda alle casse aperte e decide se aprirne alcune o chiuderne delle altre
		if(misura_distanza_temporale(t1, t2) >= (long)struct_con_info->D){
		
			//mi conviene, per rendere atomica l'operazione, sacrificare un po' il grado di parallelismo e acquisire TUTTE le lock che mi servono per controllare e aggiornare
			//lo stato delle casse del supermercato
			for(int i = 0; i < struct_con_info->K; i++){
				Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
				Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[i]);
			}
			
			azzera_contatore = true;	
			
			//conto le casse aperte	e quelle con massimo una persona in coda	
			for(int i = 0; i < struct_con_info->K; i++){							
				if(array_cassieri_aperti[i] == true){								//se la cassa è aperta...
					casse_aperte_attuali++;											//...me lo segno
					if(array_cassieri_numero_clienti_in_coda_direttore[i] <= 1){	//se poi in quella cassa aperta c'è pure massimo una persona...
						casse_con_massimo_una_persona_in_coda++;					//...mi segno anche questo
					}
					if(array_cassieri_numero_clienti_in_coda_direttore[i] >= struct_con_info->S2){
						esiste_una_cassa_con_S2_persone_in_coda = true;
					}
				}else{
					array_cassieri_numero_clienti_in_coda_direttore[i]=0;			//per sicurezza, tanto una cassa chiusa avrà per forza 0 clienti in coda
				}
			}
			
			if(DEBUG) fprintf(stderr, "CASSE APERTE ATTUALI PRIMA DELLE MODIFICHE (nel ciclo temporale del direttore) = %d\n", casse_aperte_attuali);
			
			c=0;
			
			//controllo se è il caso di aprirne una
			if((casse_aperte_attuali < struct_con_info->K) && (esiste_una_cassa_con_S2_persone_in_coda == true)){	//se è possibile aprire una cassa e ce n'è bisogno	
				while(ho_aperto_una_cassa == false){
					if(array_cassieri_aperti[c] == false){				
						array_cassieri_aperti[c] = true;																	//la apro
						ho_aperto_una_cassa = true;
						array_id_cassiere[c] = c;
						Pthread_create(&array_thread_id_cassiere[c], NULL, &thread_cassiere, &array_id_cassiere[c]);		//la creo
						if(DEBUG) fprintf(stderr, "Il direttore ha aperto la cassa %d\n", c);
						casse_aperte_attuali++;
					}	
					c++;											
				}
			}
			
			if(ho_aperto_una_cassa==false){			//chiudo se non ho aperto 
				c=0;
				//controllo se è il caso di chiuderne una, ma solo se prima non ne ho aperta un'altra (perché, in generale, volendo io direttore una abbondanza di clienti,
				//se sono in una situazione in cui un paio di casse sono poco frequentate ma una è sovraffollata, vorrò dare il tempo a quella cassa di sfollarsi e "cedere"
				//alcuni clienti alle altre casse). Voglio, insomma, dare priorità all'apertura delle casse.
				if((casse_aperte_attuali>1) && (casse_con_massimo_una_persona_in_coda >= struct_con_info->S1)){		//se almeno due casse sono aperte e ce ne sono almeno S1 
					while(ho_chiuso_una_cassa == false){															//con max un cliente in coda
						if((array_cassieri_numero_clienti_in_coda_direttore[c] <= 1) && (array_cassieri_aperti[c]==true)){		//non appena trovo una cassa con
																																//troppe poche persone in coda CHE SIA ANCHE APERTA
							
							array_cassieri_aperti[c] = false;												//la chiudo
							Pthread_cond_signal(&CV_array_cassieri_numero_clienti_in_coda[c]);				//mando il segnale per dire alla cassa che deve chiudere!
							
							
							Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[c]);
							Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[c]);							//il cassiere che chiude deve avere le lock disponibili per terminare!
							Pthread_join(array_thread_id_cassiere[c], NULL);
							Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[c]);							//ma poi me le riprendo subito
							Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[c]);
							
							if(DEBUG) fprintf(stderr, "Il direttore ha chiuso la cassa %d\n", c);
							ho_chiuso_una_cassa = true;														//segnalo a me stesso di aver chiuso una cassa
							casse_aperte_attuali--;
						}
						c++;
					}
				}
			}
			
			if(DEBUG) fprintf(stderr, "CASSE APERTE ATTUALI DOPO LE MODIFICHE (nel ciclo temporale del direttore) = %d\n", casse_aperte_attuali);
			
			//libero, tutte insieme, le lock che avevo acquisito all'inizio
			for(int i = 0; i < struct_con_info->K; i++){
				Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda_direttore[i]);
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
			}
		}
		
		if(DEBUG) fprintf(stderr, "FINE CICLO WHILE DIRETTORE\n");
		
		while(notifica_direttore_cambiamento == false){
			Pthread_mutex_lock(&MUTEX_notifica_direttore_cambiamento);							
			Pthread_cond_wait(&CV_notifica_direttore_cambiamento, &MUTEX_notifica_direttore_cambiamento);	//il direttore, dopo aver fatto un controllo completo del supermercato,
		}																									//si mette in attesa di un cambiamento, che gli verrà comunicato da un cassiere
		notifica_direttore_cambiamento = false;
		Pthread_mutex_unlock(&MUTEX_notifica_direttore_cambiamento);										
	}													//esco dal while quando ho ricevuto un segnale di chiusura SIGHUP o SIGQUIT

	
	bool sono_usciti_tutti_i_clienti = false;					//variabile che mi servirà per capire quando posso davvero terminare nel caso di sighup
	int casse_ancora_aperte_con_almeno_un_cliente_in_coda = 0;
	int casse_ancora_aperte_prima_della_eventuale_chiusura = 0;	//variabile che serve a fare in modo che almeno una cassa rimanga aperta nel while così che i clienti residui possano uscire
	
	if(sighup_arrivato==1){
		if(DEBUG) fprintf(stderr, "E' ARRIVATO IL SIGHUP AL DIRETTORE\n");
		
		while(sono_usciti_tutti_i_clienti==false){		
			clienti_attuali=0;
			casse_ancora_aperte_con_almeno_un_cliente_in_coda=0;
			casse_ancora_aperte_prima_della_eventuale_chiusura=0;
			
			if(DEBUG) fprintf(stderr, "Inizio while del sighup arrivato\n");
			//devo contare il numero di casse aperte per sapere quante sto aspettando che chiudano
			for(int i = 0; i < struct_con_info->K; i++){
				Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
				if(array_cassieri_aperti[i]==true){									//conto quante casse sono aperte prima di agire su di loro, ovvero prima di (eventualmente) chiuderle
					casse_ancora_aperte_prima_della_eventuale_chiusura++;
				}
			}
			for(int i = 0; i < struct_con_info->K; i++){		
				Pthread_mutex_lock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);
				//se la cassa è aperta ma non ci sono persone in coda, la chiudo subito (A MENO CHE non sia l'ultima cassa rimasta aperta. Almeno una cassa aperta mi serve!)
				if(array_cassieri_aperti[i]==true && array_cassieri_numero_clienti_in_coda[i]==0 && casse_ancora_aperte_prima_della_eventuale_chiusura>=2){
					if(DEBUG) fprintf(stderr, "IL DIRETTORE STA CHIUDENDO LA CASSA %d PERCHE' AL MOMENTO HA 0 CLIENTI IN CODA\n", i);
					array_cassieri_aperti[i]=false;
					Pthread_cond_signal(&CV_array_cassieri_numero_clienti_in_coda[i]);						//informo il cassiere che può chiudere
					Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);					//libero le lock così che il cassiere possa terminare
					Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
					Pthread_join(array_thread_id_cassiere[i], NULL);
					Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
					casse_ancora_aperte_prima_della_eventuale_chiusura--;
				}else if(array_cassieri_aperti[i]==true && array_cassieri_numero_clienti_in_coda[i]>0){		//se la cassa è aperta ma ha delle persone in coda, la conto come ancora aperta
					casse_ancora_aperte_con_almeno_un_cliente_in_coda++;	
					Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);
				}else{
					Pthread_mutex_unlock(&MUTEX_array_cassieri_numero_clienti_in_coda[i]);
				}
			}
			for(int i = 0; i < struct_con_info->K; i++){
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
			}
			
			//arrivati a questo punto, casse_ancora_aperte_con_almeno_un_cliente_in_coda è una variabile che mi dice quante sono le casse aperte che stanno ancora servendo dei
			//clienti, ovvero, quante sono le casse che al momento del controllo avevano almeno un cliente in coda. Ogni cassiere, quando termina di servire i suoi clienti
			//(dopo l'arrivo di sighup), aumenta di uno il valore di sighup_numero_casse_concluse, così che il direttore, quando tutte le casse di cui stava aspettando la terminazione
			//sono terminate, possa guardare le presenze dei clienti in modo più preciso. Ma almeno una cassa DEVE essere lasciata aperta quando questo controllo è stato effettuato,
			//ovvero quando giungiamo al termine di questo while.
			
			//il direttore non torna operativo finché le casse "al lavoro", ovvero quelle che ancora avevano almeno un cliente in coda, non hanno finito di servire tutti i clienti
			//che hanno in coda. Ma, allo stesso tempo, UNA, almeno UNA cassa, ci piacerebbe lasciarla aperta, così che i clienti ritardatari possano trovarla e mettercisi in coda.
			Pthread_mutex_lock(&MUTEX_sighup_numero_casse_concluse);
			while((sighup_numero_casse_concluse < casse_ancora_aperte_con_almeno_un_cliente_in_coda) && (casse_ancora_aperte_con_almeno_un_cliente_in_coda >= 2)){	
				Pthread_cond_wait(&CV_sighup_numero_casse_concluse, &MUTEX_sighup_numero_casse_concluse);	
				casse_ancora_aperte_con_almeno_un_cliente_in_coda--;										//il numero di casse aperte con almeno un cliente in coda sono scese di uno,
																											//dato che ne abbiamo chiusa una che ci ha appena informato del fatto che
																											//ha 0 clienti
			}																									
			Pthread_mutex_unlock(&MUTEX_sighup_numero_casse_concluse);
			
			//conto i clienti presenti nel supermercato. Alcuni saranno usciti e andrà liberata la loro memoria, altri staranno aspettando il mio permesso per uscire
			Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
			for(int i = 0; i < struct_con_info->C; i++){								//conto i clienti nel supermercato
				if((array_id_cliente_presente_nel_supermercato[i] != -1) && (array_id_cliente_presente_nel_supermercato[i] != -2)){
					clienti_attuali++;
					if(DEBUG) fprintf(stderr, "CLIENTE RIMASTO: %d\n", i);
					
				}else if(array_id_cliente_presente_nel_supermercato[i] == -1){			//se il cliente i-esimo ha come stato -1, vuol dire che è uscito ma devo liberare la sua memoria
					Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
					Pthread_join(array_thread_id_clienti[i], NULL);						//quindi lo joino
					Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
					array_id_cliente_presente_nel_supermercato[i] = -2;					//e poi setto il suo stato a -2, così da non fare una seconda join su di lui
				}
			}
			Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
			
			if(DEBUG){
				Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
				for(int i = 0; i < struct_con_info->C; i++){
					fprintf(stderr, "Alla chiusura (sighup) lo stato del cliente %d è %d\n", i, array_id_cliente_presente_nel_supermercato[i]);
				}
				Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
			}
			
			
			//poi guardo se tra i clienti rimasti nel supermercato ce n'è qualcuno che sta aspettando il mio permesso per uscire
			Pthread_mutex_lock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
			if(clienti_con_zero_prodotti_in_attesa > 0){								//se i clienti in attesa del direttore esistono 
				if(DEBUG) fprintf(stderr, "IL DIRETTORE, ALLA CHIUSURA DEL SIGHUP, HA RILEVATO ALMENO UN CLIENTE CON 0 PRODOTTI.\n");
				Pthread_mutex_lock(&MUTEX_direttore_tail);
				delete_director();
				clienti_con_zero_prodotti_in_attesa=0;
				Pthread_mutex_unlock(&MUTEX_direttore_tail);
			}
			Pthread_mutex_unlock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
				
			//infine, se i clienti sono tutti usciti, si può chiudere definitivamente il supermercato
			if(clienti_attuali==0){
				sono_usciti_tutti_i_clienti = true;
				if(DEBUG) fprintf(stderr, "FINALMENTE SONO USCITI TUTTI I CLIENTI\n");
			}else{
				Pthread_mutex_lock(&MUTEX_uscito_un_cliente_dal_supermercato);
				if(uscito_un_cliente_dal_supermercato == false){
					if(DEBUG) fprintf(stderr, "Il direttore attende un segnale da un cliente\n");
					Pthread_cond_wait(&CV_uscito_un_cliente_dal_supermercato, &MUTEX_uscito_un_cliente_dal_supermercato);
					if(DEBUG) fprintf(stderr, "Il direttore ha ricevuto un segnale da un cliente\n");
				}
				uscito_un_cliente_dal_supermercato = false;
				Pthread_mutex_unlock(&MUTEX_uscito_un_cliente_dal_supermercato);
			}
			
			Pthread_mutex_lock(&MUTEX_sighup_numero_casse_concluse);
			sighup_numero_casse_concluse=0;
			Pthread_mutex_unlock(&MUTEX_sighup_numero_casse_concluse);
			
			if(DEBUG) fprintf(stderr, "ANCORA NON SONO USCITI TUTTI I CLIENTI. NE SONO RIMASTI %d\n", clienti_attuali);
		}
		
		//una volta che sono usciti tutti i clienti, chiudo ogni cassa, e faccio la join per liberare la memoria
		for(int i = 0; i < struct_con_info->K; i++){
			Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
			if(array_cassieri_aperti[i]==true){														//devo chiudere una cassa solo se è aperta
				array_cassieri_aperti[i] = false;													//chiudo la cassa
				Pthread_cond_signal(&CV_array_cassieri_numero_clienti_in_coda[i]);					//informo il cassiere che deve chiudere mediante una signal
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
				Pthread_join(array_thread_id_cassiere[i], NULL);
				if(DEBUG) fprintf(stderr, "IL DIRETTORE HA CHIUSO LA CASSA %d PERCHE' SONO USCITI TUTTI I CLIENTI DAL SUPERMERCATO\n", i);
			}else{
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
			}
		}
		if(DEBUG) fprintf(stderr, "TUTTI I CASSIERI HANNO CHIUSO\n");
		
	}
	
	
	if(sigquit_arrivato==1){
		//se è arrivato il segnale di sigquit vuol dire che i clienti ancora presenti nel supermercato e quelli in coda non devono essere serviti ma fatti uscire subito
		//per fare ciò, il direttore farà chiudere TUTTE le casse, e o lui farà uscire i clienti, o usciranno da soli
		if(DEBUG) fprintf(stderr, "E' arrivato il sigquit al direttore\n");
		Pthread_mutex_lock(&MUTEX_si_chiude_sigquit);
		si_chiude_sigquit=true;								//così i clienti che si metteranno a cercare una coda a seguito di una chiusura capiranno che devono uscire
		Pthread_mutex_unlock(&MUTEX_si_chiude_sigquit);
		for(int i = 0; i < struct_con_info->K; i++){
			Pthread_mutex_lock(&MUTEX_array_cassieri_aperti[i]);
			if(array_cassieri_aperti[i]==true){				//in questo modo i cassieri settano la variabile di tutti i clienti in coda a 3. Una gestione appropriata dell'evento
															//nel thread cliente permetterà loro di uscire subito dal supermercato
				array_cassieri_aperti[i]=false;
				Pthread_cond_signal(&CV_array_cassieri_numero_clienti_in_coda[i]);		//informo il cassiere che deve chiudere mediante una signal
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
				Pthread_join(array_thread_id_cassiere[i], NULL);
			}else{
				Pthread_mutex_unlock(&MUTEX_array_cassieri_aperti[i]);
			}
		}
		
		//come nel sighup. In questo caso, avendo PRIMA chiuso le casse e POI essermi occupato dei clienti, questi clienti non sono stati serviti, ma la loro memoria è stata liberata.
		while(sono_usciti_tutti_i_clienti==false){
			clienti_attuali=0;
			//prima di tutti conto i clienti presenti nel supermercato. Alcuni di questi saranno in coda ad una cassa o stanno per andarci, altri stanno aspettando il mio permesso
			Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
			for(int i = 0; i < struct_con_info->C; i++){								//conto i clienti nel supermercato
				if((array_id_cliente_presente_nel_supermercato[i] != -1) && (array_id_cliente_presente_nel_supermercato[i] != -2)){
					clienti_attuali++;
				}else if(array_id_cliente_presente_nel_supermercato[i] == -1){			//se il cliente i-esimo ha come stato -1, vuol dire che è uscito ma devo liberare la sua memoria
					Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
					Pthread_join(array_thread_id_clienti[i], NULL);						//quindi lo joino
					Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
					array_id_cliente_presente_nel_supermercato[i] = -2;					//e poi setto il suo stato a -2, così da non fare una seconda join su di lui
				}
			}
			Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
				
			//poi guardo se tra i clienti rimasti nel supermercato ce n'è qualcuno che sta aspettando il mio permesso per uscire
			Pthread_mutex_lock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
			if(clienti_con_zero_prodotti_in_attesa > 0){								//se i clienti in attesa del direttore esistono 
				Pthread_mutex_lock(&MUTEX_direttore_tail);
				delete_director();
				clienti_con_zero_prodotti_in_attesa=0;
				Pthread_mutex_unlock(&MUTEX_direttore_tail);
				if(DEBUG) fprintf(stderr, "IL DIRETTORE, ALLA CHIUSURA DEL SIGQUIT, HA RILEVATO UN CLIENTE CON 0 PRODOTTI.\n");
			}
			Pthread_mutex_unlock(&MUTEX_clienti_con_zero_prodotti_in_attesa);
			
			if(DEBUG){
				Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
				for(int i = 0; i < struct_con_info->C; i++){
					fprintf(stderr, "Alla chiusura (sigquit) lo stato del cliente %d è %d\n", i, array_id_cliente_presente_nel_supermercato[i]);
				}
				Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
			}
				
			//infine, se i clienti sono tutti usciti, si può chiudere definitivamente il supermercato
			if(clienti_attuali==0){
				sono_usciti_tutti_i_clienti = true;
			}	
		}
	}
	
	if(DEBUG) fprintf(stderr, "Il direttore sta chiudendo tutto\n");
	
	free(struct_con_info);
	free(array_thread_id_cassiere);
	free(array_id_cassiere);
	free(array_thread_id_clienti);
	free(array_id_clienti);
	
	return NULL;
	
}


//funzione per inserire un nuovo cliente nella coda dei clienti con 0 prodotti in attesa dell'autorizzazione del direttore per poter uscire
void insert_director(int id_cli){
	clienti_attesa_direttore* new = (clienti_attesa_direttore*)malloc(sizeof(clienti_attesa_direttore));
	if(new==NULL){
		printf("Errore di allocazione memoria alla linea %d\n", __LINE__);
		exit(EXIT_FAILURE);
	}
	new->id_cliente = id_cli;
	new->next = NULL;
	if(direttore_tail==NULL){
		direttore_tail=new;
	}else{
		new->next = direttore_tail;
		direttore_tail = new;	
	}

}

//funzione per eliminare i clienti che si sono messi in coda direttore ad aspettare il segnale del direttore per poter uscire
void delete_director(){
	clienti_attesa_direttore* aux = NULL;
	aux = direttore_tail;
	clienti_attesa_direttore* aux2 = NULL;
	aux2 = aux;
	direttore_tail = NULL;															//tanto mi sono salvato con aux la tail corrente. Alla fine della chiamata la coda direttore sarà vuota
	while(aux!=NULL){
		Pthread_mutex_lock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		array_id_cliente_presente_nel_supermercato[aux->id_cliente] = -1;			//faccio uscire il cliente
		if(DEBUG){
			if(sighup_arrivato==1){
				fprintf(stderr, "IL CLIENTE CON 0 PRODOTTI RIMOSSO DAL DIRETTORE E' IL NUMERO %d\n", aux->id_cliente);
			}
		}
		Pthread_mutex_unlock(&MUTEX_array_id_cliente_presente_nel_supermercato);
		aux2 = aux;																	//mi salvo il cliente che ho fatto appena uscire per poter liberare la sua memoria
		aux=aux->next;
		free(aux2);
	}
}

//handlers per le due interruzioni che causano la chiusura del supermercato

void handler_sigquit(){
	sigquit_arrivato = 1;
}

void handler_sighup(){
	sighup_arrivato = 1;
}



