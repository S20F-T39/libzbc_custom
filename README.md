# libzbc_custom

libzbc를 커스텀하여 파일 입출력을 수행합니다.

> write 시에 zone 을 지정하는 방법
1. `Conventional Zone` 은 일단 건드리지 않는다. `Conventional zone`을 무시한 채로 진행.
2. `Empty Zone` 의 첫번째를 구하여 `Write`
3. `Empty Zone` 이 없고 `Implicit Open` 이 있을 때에는 
그 `Implicit Open Zone` 의 `Write Pointer` 를 구하여
해당 `Write Pointer` 부터 작성

> zbc_list_zones 함수
1. `empty_zones` 변수에 상태가 `Empty` 인 zone 담음 (배열이라고 생각하면 간단?)
2. nr_zone 변수에 zone 개수를 지정해 준다.

