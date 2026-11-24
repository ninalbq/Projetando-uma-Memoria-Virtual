Projetando uma Memória Virtual

Trabalho de Infraestrutura de Software (Implementação 6), baseado no exercício "Designing a Virtual Memory Manager" do livro Operating System Concepts.

O programa pega endereços lógicos de um arquivo e traduz para endereços físicos usando TLB e tabela de páginas. A memória física tem só 128 frames, então quando enche é preciso substituir páginas (FIFO ou LRU).

Como compilar
make

Isso gera o executável vm. Para apagar o executável e o correct.txt:

make clean
Como rodar
./vm <arquivo_enderecos> <fifo|lru> <fifo|lru>
1º argumento: arquivo com os endereços lógicos (ex: addresses.txt)
2º argumento: algoritmo de substituição de página
3º argumento: algoritmo de substituição da TLB

Exemplo: página com LRU e TLB com FIFO

./vm addresses.txt lru fifo

O BACKING_STORE.bin e o addresses.txt (do professor) precisam estar na mesma pasta de onde o programa é executado. A saída vai para o arquivo correct.txt.

O que o programa faz
Página e frame de 256 bytes, 256 páginas, 128 frames, TLB com 16 entradas.
O endereço de 16 bits é dividido em número da página (8 bits de cima) e deslocamento (8 bits de baixo).
A busca na TLB usa uma thread para cada entrada (16 threads), e a principal espera todas terminarem com pthread_join.
Se der hit na TLB, usa o frame de lá. Se não, olha a tabela de páginas. Se a página não estiver na memória, ocorre page fault e ela é lida do BACKING_STORE.bin.
Os frames são preenchidos de 0 a 127. Depois que enche, o frame que sai é o mais antigo (FIFO) ou o usado há mais tempo (LRU).
Quando uma página sai da memória, a entrada dela na TLB também é invalidada.
Na TLB usa primeiro entrada livre; se estiver cheia, tira por FIFO ou LRU.
Saída

Cada linha do correct.txt tem o formato:

Virtual address: 16916 TLB: 0 Physical address: 20 Value: 0

O número depois de TLB: é o índice da entrada da TLB usada. No final aparece o resumo (total de endereços, page faults, taxa de page fault, TLB hits e taxa de hit).

Tratamento de erros

O programa mostra uma mensagem de erro e encerra (sem quebrar) quando:

o número de argumentos não é 3
o algoritmo não é fifo nem lru
o arquivo de endereços não existe ou não abre
o arquivo de endereços tem alguma linha que não é um número de 0 a 65535, ou está vazio
o BACKING_STORE.bin não abre