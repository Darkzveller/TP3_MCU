/* TP3 - STM32F103RB - parties 2.1 et 2.2, CMSIS classique.
 * Apres configuration de l'horloge : init_timer(); init_pin_onewire();
 * TIM3 est reserve au OneWire. Ajouter une resistance de 4,7 kOhm
 * entre PB4 (DQ) et 3,3 V. Relier les masses de la carte et du badge.
 */
#include "stm32f10x.h"

#define OC1(mode) ((mode) << 4)
#define OC2(mode) ((mode) << 12)
#define ACTIF_SUR_COMPARAISON 1U
#define INACTIF_SUR_COMPARAISON 2U
#define FORCE_INACTIF 4U
#define FORCE_ACTIF 5U

// Variables partagées entre le programme principal et l'interruption
volatile uint8_t etat_one_wire;
volatile uint8_t motif_one_wire_fini;


/* 2.1 : PB4 = DQ, PB5 = temoin de l'instant de lecture. */
void init_pin_onewire(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPBEN;
	(void)RCC->APB2ENR;

	/* TIM3_REMAP = 10 : remap partiel. SWJ_CFG = 010 : SWD conserve,
	 * JTAG desactive pour liberer PB4 (NJTRST). */
	uint32_t map = AFIO->MAPR;
	map &= ~((3U << 10) | (7U << 24));
	AFIO->MAPR = map | (2U << 10) | (2U << 24);

	/* Chaque broche occupe 4 bits de CRL. Sorties a 2 MHz :
	 * PB4 = 1110 (fonction alternee, drain ouvert)
	 * PB5 = 1010 (fonction alternee, push-pull). */
	uint32_t crl = GPIOB->CRL;
	crl &= ~((0xFU << 16) | (0xFU << 20));
	GPIOB->CRL = crl | (0xEU << 16) | (0xAU << 20);
}

/* 2.2 : initialisation commune, une fois au demarrage.
 * L'horloge de TIM3 doit etre un multiple entier de 1 MHz. */
void init_timer(void)
{
	SystemCoreClockUpdate(); /* SystemCoreClock = HCLK */
	uint32_t ppre1 = (RCC->CFGR >> 8) & 7U;
	uint32_t tim3_hz = SystemCoreClock;
	if (ppre1 >= 4U)
	{
		/* APB1 divise HCLK par 2, 4, 8 ou 16. TIM3 recoit 2 x PCLK1. */
		tim3_hz >>= (ppre1 - 4U);
	}

	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	(void)RCC->APB1ENR;
	RCC->APB1RSTR |= RCC_APB1RSTR_TIM3RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM3RST;

	TIM3->PSC = tim3_hz / 1000000U - 1U;
	TIM3->ARR = 0xFFFFU;
	/* Au repos : CH1 actif = drain ouvert relache, CH2 inactif = 0. */
	TIM3->CCMR1 = OC1(FORCE_ACTIF) | OC2(FORCE_INACTIF);
	TIM3->CCMR2 = 0U; /* CH3 en comparaison interne */
	TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
	TIM3->EGR = TIM_EGR_UG; /* Charge le prescaler */
	TIM3->SR = 0U;
	NVIC_DisableIRQ(TIM3_IRQn);
	TIM3->DIER = TIM_DIER_CC2IE | TIM_DIER_CC3IE;
	/* Partie 2.3 : activer le NVIC une fois TIM3_IRQHandler defini. */
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);       // Autorise l'appel de TIM3_IRQHandler

}

/* 2.2 : instants en microsecondes depuis le depart du compteur.
 * Appeler au repos, avec 0 < D1,D2 < D3. */
void init_motif(uint16_t D1, uint16_t D2, uint16_t D3)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;  // Arrêter le compteur
    TIM3->CCR1 = D1;
    TIM3->CCR2 = D2;
    TIM3->CCR3 = D3;
    TIM3->EGR = TIM_EGR_UG;     // Remettre le compteur à zéro
    TIM3->SR = 0U;

    motif_one_wire_fini = 0U;

    TIM3->DIER = TIM_DIER_CC3IE;       // CH3 : fin du motif
    if (D2 < D3) {
        TIM3->DIER |= TIM_DIER_CC2IE;  // CH2 : lecture prévue avant la fin
    }

    uint32_t masque_it = __get_PRIMASK();
    __disable_irq();

    TIM3->CCMR1 = OC1(FORCE_INACTIF) | OC2(FORCE_ACTIF);
    TIM3->CCMR1 = OC1(ACTIF_SUR_COMPARAISON) | OC2(INACTIF_SUR_COMPARAISON);
    TIM3->CR1 |= TIM_CR1_CEN;          // Tout est prêt : démarrer TIM3

    __set_PRIMASK(masque_it);
}
void TIM3_IRQHandler(void)
{
	// CH2 : à D2, lire le bit sur PB4 si une lecture est prévue
	if ((TIM3->SR & TIM_SR_CC2IF) && (TIM3->DIER & TIM_DIER_CC2IE))
	{
		etat_one_wire = (GPIOB->IDR >> 4) & 1U;
		TIM3->SR = (uint16_t)~TIM_SR_CC2IF;
	}

	// CH3 : à D3, arrêter le timer et signaler la fin du motif
	if (TIM3->SR & TIM_SR_CC3IF)
	{
		TIM3->CR1 &= ~TIM_CR1_CEN;
		TIM3->SR = (uint16_t)~TIM_SR_CC3IF;
		motif_one_wire_fini = 1U;
	}
}
int main(void)
{
    init_timer();
    init_pin_onewire();

    init_motif(6, 13, 70);

    while (motif_one_wire_fini == 0U) {
        // Attendre que CH3 signale la fin du motif
    }

    // Le motif est terminé. Observer etat_one_wire dans le débogueur.
    while (1) {
    }
}