# libzbc_custom

libzbc를 커스텀하여 파일 입출력을 수행합니다.

> write 시에 zone 을 지정하는 방법
1. `Conventional Zone` 은 일단 건드리지 않는다.
사용자가 지정한 `Conventional Zone` 개수를 구하는 법이 있는가?
2. `Empty Zone` 의 첫번째를 구하여 `Write`
3. `Empty Zone` 이 없고 `Implicit Open` 이 있을 때에는 
그 `Implicit Open Zone` 의 `Write Pointer` 를 구하여
해당 `Write Pointer` 부터 작성