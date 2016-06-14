/*
 *  kernel/kernel.c
 *
 *  Minikernel. Versión 1.0
 *
 *  Fernando Pérez Costoya
 *
 */

/*
 *
 * Fichero que contiene la funcionalidad del sistema operativo
 *
 */
#include <string.h>
#include "kernel.h"	/* Contiene defs. usadas por este modulo */

/*
 *
 * Funciones relacionadas con la tabla de procesos:
 *	iniciar_tabla_proc buscar_BCP_libre
 *
 */

/*
 * Función que inicia la tabla de procesos
 */
static void iniciar_tabla_proc(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		tabla_procs[i].estado=NO_USADA;
}

/*
 * Función que busca una entrada libre en la tabla de procesos
 */
static int buscar_BCP_libre(){
	int i;

	for (i=0; i<MAX_PROC; i++)
		if (tabla_procs[i].estado==NO_USADA)
			return i;
	return -1;
}

/*
 *
 * Funciones que facilitan el manejo de las listas de BCPs
 *	insertar_ultimo eliminar_primero eliminar_elem
 *
 * NOTA: PRIMERO SE DEBE LLAMAR A eliminar Y LUEGO A insertar
 */

/*
 * Inserta un BCP al final de la lista.
 */
static void insertar_ultimo(lista_BCPs *lista, BCP * proc){
	if (lista->primero==NULL)
		lista->primero= proc;
	else
		lista->ultimo->siguiente=proc;
	lista->ultimo= proc;
	proc->siguiente=NULL;
}

/*
 * Elimina el primer BCP de la lista.
 */
static void eliminar_primero(lista_BCPs *lista){

	if (lista->ultimo==lista->primero)
		lista->ultimo=NULL;
	lista->primero=lista->primero->siguiente;
}

/*
 * Elimina un determinado BCP de la lista.
 */
static void eliminar_elem(lista_BCPs *lista, BCP * proc){
	BCP *paux=lista->primero;

	if (paux==proc)
		eliminar_primero(lista);
	else {
		for ( ; ((paux) && (paux->siguiente!=proc));
			paux=paux->siguiente);
		if (paux) {
			if (lista->ultimo==paux->siguiente)
				lista->ultimo=paux;
			paux->siguiente=paux->siguiente->siguiente;
		}
	}
}

/*
 *
 * Funciones relacionadas con la planificacion
 *	espera_int planificador
 */

/*
 * Espera a que se produzca una interrupcion
 */
static void espera_int(){
	int nivel;

	//printk("-> NO HAY LISTOS. ESPERA INT\n");

	/* Baja al mínimo el nivel de interrupción mientras espera */
	nivel=fijar_nivel_int(NIVEL_1);
	halt();
	fijar_nivel_int(nivel);
}

/*
 * Función de planificacion que implementa un algoritmo FIFO.
 */
static BCP * planificador(){
	while (lista_listos.primero==NULL)
		espera_int();		/* No hay nada que hacer */

	// Asigna rodaja al proceso
	BCP *proceso = lista_listos.primero;
	proceso->ticksRestantesRodaja = TICKS_POR_RODAJA;

	return lista_listos.primero;
}

/*
 *
 * Funcion auxiliar que termina proceso actual liberando sus recursos.
 * Usada por llamada terminar_proceso y por rutinas que tratan excepciones
 *
 */
static void liberar_proceso(){
	BCP * p_proc_anterior;

	liberar_imagen(p_proc_actual->info_mem); /* liberar mapa */

	p_proc_actual->estado=TERMINADO;

	int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
	eliminar_primero(&lista_listos); /* proc. fuera de listos */
	fijar_nivel_int(nivel_interrupciones);

	/* Realizar cambio de contexto */
	p_proc_anterior=p_proc_actual;
	p_proc_actual=planificador();

	printk("-> C.CONTEXTO POR FIN: de %d a %d\n",
			p_proc_anterior->id, p_proc_actual->id);

	liberar_pila(p_proc_anterior->pila);
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
        return; /* no debería llegar aqui */
}

/*
 *
 * Funciones relacionadas con el tratamiento de interrupciones
 *	excepciones: exc_arit exc_mem
 *	interrupciones de reloj: int_reloj
 *	interrupciones del terminal: int_terminal
 *	llamadas al sistemas: llam_sis
 *	interrupciones SW: int_sw
 *
 */

/*
 * Tratamiento de excepciones aritmeticas
 */
static void exc_arit(){

	if (!viene_de_modo_usuario())
		panico("excepcion aritmetica cuando estaba dentro del kernel");


	printk("-> EXCEPCION ARITMETICA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debería llegar aqui */
}

/*
 * Tratamiento de excepciones en el acceso a memoria
 */
static void exc_mem(){

	if(accesoParam == 0){
		if (!viene_de_modo_usuario()){
			panico("excepcion de memoria cuando estaba dentro del kernel");
		}
	}

	printk("-> EXCEPCION DE MEMORIA EN PROC %d\n", p_proc_actual->id);
	liberar_proceso();

        return; /* no debería llegar aqui */
}

/*
 * Tratamiento de interrupciones de terminal
 */
static void int_terminal(){
	char car;
	car = leer_puerto(DIR_TERMINAL);
	printk("-> TRATANDO INT. DE TERMINAL %c\n", car);

	// si el buffer no está lleno introduce el caracter nuevo
	if(caracteresEnBuffer < TAM_BUF_TERM){
		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		bufferCaracteres[caracteresEnBuffer] = car;
		caracteresEnBuffer++;
		

		// desbloquea procesos bloqueados
		BCP *proceso_bloqueado = lista_bloqueados.primero;
	
		int desbloqueado = 0;
		if (proceso_bloqueado != NULL){
			if(proceso_bloqueado->bloqueadoPorLectura == 1){
				// Desbloquear proceso
				desbloqueado = 1;
				proceso_bloqueado->estado = LISTO;
				proceso_bloqueado->bloqueadoPorLectura = 0;
				eliminar_elem(&lista_bloqueados, proceso_bloqueado);
				insertar_ultimo(&lista_listos, proceso_bloqueado);
			}
		}
		
		while(desbloqueado != 1 && proceso_bloqueado != lista_bloqueados.ultimo){
			proceso_bloqueado = proceso_bloqueado->siguiente;
			if(proceso_bloqueado->bloqueadoPorLectura == 1){
				// Desbloquear proceso
				desbloqueado = 1;
				proceso_bloqueado->estado = LISTO;
				proceso_bloqueado->bloqueadoPorLectura = 0;
				eliminar_elem(&lista_bloqueados, proceso_bloqueado);
				insertar_ultimo(&lista_listos, proceso_bloqueado);
			}
		}

		fijar_nivel_int(nivel_interrupciones);
	}
    return;
}

/*
 * Tratamiento de interrupciones de reloj
 */
static void int_reloj(){

	//printk("-> TRATANDO INT. DE RELOJ\n");

	BCP *proceso_listo = lista_listos.primero;
	
	// Rellena contadores de usuario y sistema del proceso en ejecucion
	if(proceso_listo != NULL){
		if(viene_de_modo_usuario()){
			p_proc_actual->veces_usuario++;
		}
		else{
			p_proc_actual->veces_sistema++;
		}

		// Comprueba si ha terminado rodaja de tiempo del proceso
		if(p_proc_actual->ticksRestantesRodaja <= 1){
			// Si no le queda rodaja activa int SW de planificacion
			idABloquear = p_proc_actual->id;
			activar_int_SW();
		}
		else{
			// Resta tick de rodaja al proceso
			p_proc_actual->ticksRestantesRodaja--;
		}
	}

	BCP *proceso_bloqueado = lista_bloqueados.primero;

	// Incrementa contador de llamadas a int_reloj
	numTicks++;
	
	// Comprueba si hay procesos que se pueden desbloquear
	if (proceso_bloqueado != NULL) {

		// Calculo de tiempo de bloqueo
		int numTicksBloqueo = proceso_bloqueado->secs_bloqueo * TICK;
		int ticksTranscurridos = numTicks - proceso_bloqueado->inicio_bloqueo;
		
		// Comprueba si el proceso se debe desbloquear
		if(ticksTranscurridos >= numTicksBloqueo && 
				proceso_bloqueado->bloqueadoPorLectura == 0){
			proceso_bloqueado->estado = LISTO;

			// Proceso de desbloquea y pasa a estado listo
			int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
			eliminar_elem(&lista_bloqueados, proceso_bloqueado);
			insertar_ultimo(&lista_listos, proceso_bloqueado);
			fijar_nivel_int(nivel_interrupciones);
		}
	}
    return;
}

/*
 * Tratamiento de llamadas al sistema
 */
static void tratar_llamsis(){
	int nserv, res;

	nserv=leer_registro(0);
	if (nserv<NSERVICIOS)
		res=(tabla_servicios[nserv].fservicio)();
	else
		res=-1;		/* servicio no existente */
	escribir_registro(0,res);
	return;
}

/*
 * Tratamiento de interrupciones software
 */
static void int_sw(){

	printk("-> TRATANDO INT. SW\n");

	// Interrupcion SW de planificacion
	// Comprueba que proceso en ejecución es el que se quiere bloquear
	if(idABloquear == p_proc_actual->id){
		// Pone el proceso ejecutando al final de la cola de listos
		BCP *proceso = lista_listos.primero;
		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		eliminar_elem(&lista_listos, proceso);
		insertar_ultimo(&lista_listos, proceso);
		fijar_nivel_int(nivel_interrupciones);

		// Cambio de contexto por int sw de planificación
		BCP *p_proc_bloqueado = p_proc_actual;
		p_proc_actual = planificador();
		cambio_contexto(&(p_proc_bloqueado->contexto_regs), &(p_proc_actual->contexto_regs));
	}

	return;
}

/*
 *
 * Funcion auxiliar que crea un proceso reservando sus recursos.
 * Usada por llamada crear_proceso.
 *
 */
static int crear_tarea(char *prog){

	void * imagen, *pc_inicial;
	int error=0;
	int proc;
	BCP *p_proc;

	proc=buscar_BCP_libre();
	if (proc==-1)
		return -1;	/* no hay entrada libre */

	/* A rellenar el BCP ... */
	p_proc=&(tabla_procs[proc]);

	/* crea la imagen de memoria leyendo ejecutable */
	imagen=crear_imagen(prog, &pc_inicial);
	if (imagen)
	{
		p_proc->info_mem=imagen;
		p_proc->pila=crear_pila(TAM_PILA);
		fijar_contexto_ini(p_proc->info_mem, p_proc->pila, TAM_PILA,
			pc_inicial,
			&(p_proc->contexto_regs));
		p_proc->id=proc;
		p_proc->estado=LISTO;

		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		/* lo inserta al final de cola de listos */
		insertar_ultimo(&lista_listos, p_proc);
		fijar_nivel_int(nivel_interrupciones);
		error= 0;
	}
	else
		error= -1; /* fallo al crear imagen */

	return error;
}

/*
 *
 * Rutinas que llevan a cabo las llamadas al sistema
 *	sis_crear_proceso sis_escribir
 *
 */

/*
 * Tratamiento de llamada al sistema crear_proceso. Llama a la
 * funcion auxiliar crear_tarea sis_terminar_proceso
 */
int sis_crear_proceso(){
	char *prog;
	int res;

	printk("-> PROC %d: CREAR PROCESO\n", p_proc_actual->id);
	prog=(char *)leer_registro(1);
	res=crear_tarea(prog);	

	return res;
}

/*
 * Tratamiento de llamada al sistema escribir. Llama simplemente a la
 * funcion de apoyo escribir_ker
 */
int sis_escribir()
{
	char *texto;
	unsigned int longi;

	texto=(char *)leer_registro(1);
	longi=(unsigned int)leer_registro(2);

	escribir_ker(texto, longi);
	return 0;
}

/*
 * Tratamiento de llamada al sistema terminar_proceso. Llama a la
 * funcion auxiliar liberar_proceso
 */
int sis_terminar_proceso(){

	printk("-> FIN PROCESO %d\n", p_proc_actual->id);

	liberar_proceso();

        return 0; /* no debería llegar aqui */
}

/*
* Funciones adicionales implementadas
*
*/

// Devuelve el identificador del proceso que la invoca
int sis_obtener_id_pr(){
	return p_proc_actual->id;
}

// El proceso se queda bloqueado los segundos especificados
int sis_dormir(){
	unsigned int numSegundos;
	int nivel_interrupciones;
	numSegundos = (unsigned int)leer_registro(1);

	// actualiza BCP con el num de segundos y cambia estado a bloqueado
	p_proc_actual->estado = BLOQUEADO;
	p_proc_actual->inicio_bloqueo = numTicks;
	p_proc_actual->secs_bloqueo = numSegundos;	

	// Guarda el nivel anterior de interrupcion y lo fija a 3
	nivel_interrupciones = fijar_nivel_int(NIVEL_3);
	
	// 1. Saca de la lista de procesos listos el BCP del proceso
	eliminar_elem(&lista_listos, p_proc_actual);

	// 2. Inserta el BCP del proceso en la lista de bloqueados
	insertar_ultimo(&lista_bloqueados, p_proc_actual);

	// Restaura el nivel de interrupcion anterior
	fijar_nivel_int(nivel_interrupciones);

	// Cambio de contexto voluntario
	BCP *p_proc_dormido = p_proc_actual;
	p_proc_actual = planificador();
	cambio_contexto(&(p_proc_dormido->contexto_regs), &(p_proc_actual->contexto_regs));

	return 0;
}

int sis_tiempos_proceso(){

	struct tiempos_ejec *tiempos_ejecucion;

	// Comprueba si existe argumento
	tiempos_ejecucion = (struct tiempos_ejec *)leer_registro(1);

	if(tiempos_ejecucion != NULL){
		// Si hay argumento fija variable global
		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		accesoParam = 1;
		fijar_nivel_int(nivel_interrupciones);

		// Rellena estructura con el tiempo de sistema y tiempo de usuario
		tiempos_ejecucion->sistema = p_proc_actual->veces_sistema;
		tiempos_ejecucion->usuario = p_proc_actual->veces_usuario;
	}

	return numTicks;
}

int sis_crear_mutex(){

	char *nombre = (char *)leer_registro(1);
	int tipo = (int)leer_registro(2);

	// Comprueba tamaño de nombre
	if(strlen(nombre) > MAX_NOM_MUT){
		return -1;
	}

	// Comprueba número de mutex del proceso
	if(p_proc_actual->numMutex >= NUM_MUT_PROC){
		return -2;
	}

	// Comprueba nombre único de mutex
	int i;
	for (i = 0; i < NUM_MUT; i++){
		if(array_mutex[i].nombre != NULL && 
			strcmp(array_mutex[i].nombre, nombre) == 0){
			return -3;
		}
	}

	// Compueba número de mutex en el sistema
	while(mutexExistentes >= NUM_MUT){
		// Bloquear proceso actual
		p_proc_actual->estado = BLOQUEADO;
		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		eliminar_elem(&lista_listos, p_proc_actual);
		insertar_ultimo(&lista_bloqueados, p_proc_actual);
		fijar_nivel_int(nivel_interrupciones);

		// Cambio de contexto voluntario
		BCP *proceso_bloqueado = p_proc_actual;
		p_proc_actual = planificador();
		cambio_contexto(&(proceso_bloqueado->contexto_regs), &(p_proc_actual->contexto_regs));
	
		// Vuelve a activarse y comprueba nombre único de mutex
		for (i = 0; i < NUM_MUT; i++){
			if(array_mutex[i].nombre != NULL && 
				strcmp(array_mutex[i].nombre, nombre) == 0){
				return -3;
			}
		}
	}

	int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
	mutex *mutexCreado = &(array_mutex[mutexExistentes]);
	mutexCreado->nombre = nombre;
	mutexCreado->tipo=tipo;
	mutexExistentes++;
	fijar_nivel_int(nivel_interrupciones);

	return 0;
}

int sis_abrir_mutex(){
	return 0;
}

int sis_lock(){
	return 0;
}

int sis_unlock(){
	return 0;
}

int sis_cerrar_mutex(){
	return 0;
}

int sis_leer_caracter(){

	// Si el buffer está vacío se bloquea
	while(caracteresEnBuffer == 0){
		p_proc_actual->estado = BLOQUEADO;
		p_proc_actual->bloqueadoPorLectura = 1;
		int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
		eliminar_elem(&lista_listos, p_proc_actual);
		insertar_ultimo(&lista_bloqueados, p_proc_actual);
		fijar_nivel_int(nivel_interrupciones);
		p_proc_actual = planificador();
	}

	int nivel_interrupciones = fijar_nivel_int(NIVEL_3);
	accesoParam = 1;
	fijar_nivel_int(nivel_interrupciones);

	int i;
	// Solicita el primer caracter del buffer
	nivel_interrupciones = fijar_nivel_int(NIVEL_2);
	char car = bufferCaracteres[0];
	caracteresEnBuffer--;

	// Reordena el buffer
	for (i = caracteresEnBuffer; i > 0; i--){
		bufferCaracteres[i-1] = bufferCaracteres[i];
	}
	fijar_nivel_int(nivel_interrupciones);

	return (long)car;
}

/*
 *
 * Rutina de inicialización invocada en arranque
 *
 */
int main(){
	/* se llega con las interrupciones prohibidas */

	instal_man_int(EXC_ARITM, exc_arit); 
	instal_man_int(EXC_MEM, exc_mem); 
	instal_man_int(INT_RELOJ, int_reloj); 
	instal_man_int(INT_TERMINAL, int_terminal); 
	instal_man_int(LLAM_SIS, tratar_llamsis); 
	instal_man_int(INT_SW, int_sw); 

	iniciar_cont_int();		/* inicia cont. interr. */
	iniciar_cont_reloj(TICK);	/* fija frecuencia del reloj */
	iniciar_cont_teclado();		/* inici cont. teclado */

	iniciar_tabla_proc();		/* inicia BCPs de tabla de procesos */

	/* crea proceso inicial */
	if (crear_tarea((void *)"init")<0)
		panico("no encontrado el proceso inicial");
	
	/* activa proceso inicial */
	p_proc_actual=planificador();
	cambio_contexto(NULL, &(p_proc_actual->contexto_regs));
	panico("S.O. reactivado inesperadamente");
	return 0;
}
