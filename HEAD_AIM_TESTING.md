# Primeira pessoa: mira e HUD pelo headset

Build de desenvolvimento: `Asylum-0.1.4-HEAD-AIM3`, baseado no fork
`Godoy301/Batman-Arkham-Asylum-VR`, versão 0.1.4.

Ative **FIRST PERSON** e **aim and HUD follow the headset** no painel VR.
O segundo ajuste fica ativado por padrão quando não existe configuração salva.
O analógico direito continua girando a direção base; o olhar é somado a ela.
O HUD acompanha posição e orientação do headset, incluindo inclinação, em um
único plano com profundidade para os dois olhos. Marcadores de alvos continuam
presos aos alvos no mundo.

O ajuste é salvo em `%LOCALAPPDATA%\Batman_Asylum\aa_rendering.ini`, seção
`AsylumVR`, chave `FirstPersonLookAim`. Comandos: `aa fp aim on`, `aa fp aim off`,
`aa fp status`. Desativar a opção restaura a orientação base e o HUD na sala.

### Correção HEAD-AIM3

O primeiro teste real mostrou `enabled=1`, `active=0`, `writes=0` e
`reason=not gameplay`, apesar de a câmera de primeira pessoa acompanhar o
personagem. O leitor de menu estava em estado desconhecido; a primeira versão
exigia confirmação de gameplay e nunca chegava à aplicação da mira.

HEAD-AIM3 também permite a aplicação quando o leitor está desconhecido **e**
a câmera nativa de primeira pessoa e o personagem controlado passam nas
validações de identidade, posição e proximidade. Um menu identificado, ausência
de pose, personagem inválido ou painel VR aberto continuam bloqueando a função.
O estado desconhecido continua registrado como desconhecido; nenhum offset do
leitor de menu foi alterado sem evidência do jogo.

Após reinstalar, feche o F10, mova a cabeça com o analógico parado e pressione
F2. Procure `build=Asylum-0.1.4-HEAD-AIM3`, `active=1`, `writes` aumentando e
`hudFollowing=1`. `reason=active via validated first-person pawn` identifica
essa alternativa de validação. O comportamento no headset ainda precisa ser
confirmado; a versão anterior nunca chegou a exercitar a atualização de mira.

## Compilação e alcance da validação

No Windows, use os comandos de compilação do README. O novo CTest se chama
`first_person_head_aim`. O workflow **Windows head aim checks** compila o mod
Win32 e executa cinco testes de CPU; ele não executa o jogo, testes de GPU,
OpenXR, nem compila o helper DLSS de 64 bits.

## Instalar as DLLs de teste (incluindo o manifesto)

O artefato `head-aim-test-dlls` contém somente as DLLs de teste. Use também o
ZIP completo da versão **0.1.4** nas
[releases originais](https://github.com/junglejuz/Batman-Arkham-Asylum-VR/releases).
Mantenha esse ZIP original para poder voltar à versão anterior.

1. Feche o jogo. Extraia o ZIP completo 0.1.4 para uma pasta de trabalho fora
   da pasta do jogo, por exemplo `Downloads\AsylumVR-HeadAim`.
2. Extraia o artefato de teste separadamente. Localize `batman_asylum.dll`,
   `d3d9.dll` e `xinput1_3.dll` (o artefato pode colocá-las em
   `build-native9\Release`). Copie **essas três DLLs** para a pasta de trabalho,
   ao lado de `Install-BatmanAsylumVR.bat`, substituindo as versões do pacote
   original. Preserve os demais arquivos e a pasta `files` do ZIP completo.
3. Execute `Install-BatmanAsylumVR.bat` **dessa pasta de trabalho modificada**.
   Ele instala as DLLs de teste e recria `Binaries\asylumvr_manifest.json` com
   os hashes dos arquivos instalados. As configurações VR existentes são mantidas.
4. Inicie pelo atalho VR normal e siga o teste abaixo.

Trocar DLLs diretamente em `Binaries` deixa os hashes do manifesto antigo.
Isso causa `A mod file in the game folder is missing or changed (d3d9.dll)`
antes mesmo de o jogo abrir. Se você já fez essa troca, o procedimento acima
também corrige essa instalação. Não é necessário desinstalar primeiro.
Não apague o manifesto nem remova a verificação no launcher.

Para voltar à versão original, extraia novamente o ZIP completo **sem as DLLs
de teste** para outra pasta e execute seu instalador. Ele restaura os arquivos
originais e recria o manifesto correspondente. Executar o instalador de um
pacote original não modificado também remove a funcionalidade de teste.

Os testes portáveis verificam a direção de mira contra raios independentes,
giro do controle somado ao headset, ausência de acumulação da rotação do
headset, recentralização, restauração, poses inválidas e projeção binocular
do HUD sob movimento. Eles não provam o comportamento dos gadgets no jogo.

## Uma sessão de teste no PC VR

1. Inicie pelo atalho VR. No menu principal, confirme o comportamento normal.
2. Entre no jogo e ative primeira pessoa. Sem tocar no analógico direito,
   olhe para esquerda/direita, cima/baixo e incline a cabeça. O HUD deve
   permanecer à frente, com a mesma profundidade nos dois olhos.
3. Mire com um gadget em dois alvos separados. Mova apenas a cabeça, depois
   use o analógico com a cabeça parada e, por fim, mova os dois juntos.
   Compare a mira visível com a direção/impacto real do gadget. Teste também
   a seleção de pontos do gancho: seu marcador deve continuar no alvo.
4. Olhe para um lado por alguns segundos sem mover o controle: não deve
   haver giro contínuo nem aplicação duplicada da rotação. Gire através da
   direção oposta, recentralize e repita com o outro lado.
5. Ligue/desligue a opção, abra/feche o painel VR, teste pausa/mapa, uma
   transição de câmera e retorno ao menu principal. Teste perda/retorno do
   rastreamento. Verifique controles, retorno da mira e ausência de saltos
   inesperados. Repita com DLSS e, se utilizado, AFW.
6. Diante de qualquer problema, pressione **F2**, saia normalmente e preserve
   os arquivos abaixo antes de iniciar várias novas sessões.

## Evidências necessárias

Em `%LOCALAPPDATA%\Batman_Asylum`:

- `batman_asylum.log`: sessão atual; `batman_asylum.prev.log`: sessão anterior.
  O logger já preserva uma geração anterior e descarrega cada mensagem em disco.
- `captures\A2p_*\status.txt`: inclui o identificador do build e `headAim=`.
  A mesma pasta contém as imagens disponíveis de cena, HUD e olhos.
- `captures\A2p_*\head_aim.csv`: até 256 amostras recentes, no máximo 20 por
  segundo durante atividade, mais mudanças de estado. Inclui tempo monotônico
  em milissegundos, geração, orientação solicitada/observada, quaternion e
  analógico. O histórico fica em memória e só é escrito ao capturar, permitindo
  examinar movimentos entre as mensagens de cinco segundos do log.
- `captures\A2p_history_*`: histórico estéreo solicitado por F2, quando existem
  pares completos disponíveis. Os nomes A2p são herdados do capturador original.

As linhas `[aa head aim]` são emitidas nas mudanças de estado e a cada cinco
segundos, evitando escrita em disco por frame. Elas registram:

- `requested/enabled/active`, `reason`, `gen` e `ageMs`: configuração,
  aplicação efetiva, motivo de suspensão, geração da pose e idade da última escrita;
- `menuMode`, `menuRead` e `viewportCountCap`: estado observado do leitor de
  menu, etapa que falhou e contagem/capacidade da lista de viewports. Esses
  campos também aparecem no CSV para distinguir um menu real de uma leitura
  indisponível;
- `bodyPitchYaw`, `targetPitchYaw`, `beforePitchYaw`, `readbackPitchYaw`:
  orientação base, solicitada, observada antes da atualização e relida após ela;
- `headQuat` e `stickRXRY`: orientação do headset e último analógico entregue
  pelo compositor de entrada, amostrados na atualização;
- `writes/failures/restores`, `hudGen/hudFollowing/hudComposites/hudFailures`:
  escritas confirmadas, falhas, restaurações e composição do HUD;
- `fpFrames/headFrames/headMisses`: aplicação da câmera e leitura do osso da cabeça.

Uma releitura correta prova somente que `Controller.Rotation` recebeu o valor;
ela não prova que todos os gadgets o consumiram. A atualização ocorre na
construção da câmera, depois da simulação daquele frame: a reação do gadget
pode ocorrer no próximo tick. Compare imagens/impactos e os logs existentes
`[aa grapple]` para confirmar o resultado real. `ageMs` crescente ou gerações
paradas indicam ausência de novas atualizações, mesmo se a última amostra estava ativa.

## Limitações conhecidas

O repositório identifica o menu principal, mas ainda não possui um sinal
validado para todos os menus de pausa/mapa ou todas as câmeras de cinematics.
Esta mudança suspende a mira no menu principal identificado, quando o
painel VR possui a entrada, sem pose nativa válida, sem pawn/controlador válido
ou com câmera distante do Batman. Se o estado do leitor de menu for desconhecido,
a alternativa depende da validação da câmera de primeira pessoa e do personagem
controlado, não de uma identificação positiva de gameplay. Pausa, cinematics
próximas e um menu não reconhecido com personagem próximo precisam do
teste acima; não há uma nova alegação de detecção desses estados.

Neste ambiente Linux, o build MSVC e a validação no headset não estão disponíveis.
O resultado do workflow Windows e a sessão real precisam ser consultados
separadamente; passar nos testes de CPU não significa aprovação no headset.


## HEAD-AIM3: pitch durante caminhada e bloqueio do analógico

No F10, em primeira pessoa, `block right-stick up / down` bloqueia o eixo
vertical do analógico, preservando o horizontal. Desligada por padrão, salva
em `FirstPersonBlockStickPitch`. Feche o painel para testar. O bloqueio só é
publicado enquanto a câmera em primeira pessoa é válida; polling do painel,
menus identificados e terceira pessoa mantêm a entrada original.

A base de pitch deixa de incorporar ajustes automáticos do jogo quando não há
entrada vertical deliberada do analógico. Isso evita realimentar a inclinação
injetada do headset quando o jogo recentraliza a câmera durante a caminhada.
Com a opção desligada e entrada acima da deadzone XInput, o pitch do analógico
continua somado ao olhar. Mouse vertical não é contemplado por essa política.

Teste olhar para cima/baixo parado e andando (frente, trás e lados); depois
repita com o bloqueio ligado/desligado, verificando giro horizontal e gadgets.
Faça uma NOVA captura F2 durante a caminhada se houver inversão. O log tem a
versão HEAD-AIM3; o CSV acrescenta `raw_ry`, `move_lx`, `move_ly`,
`pitch_input`, `block_stick_pitch`. `stick_ry` é a entrada entregue ao jogo.
Os testes simulam recentralização; o resultado real no headset precisa ser
confirmado. Ajustes de câmera do jogo enquanto o analógico vertical está ativo
continuam indistinguíveis da entrada do jogador no delta do controlador.
