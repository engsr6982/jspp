function main() {
	if (setDone == null) {
		throw 'setDone not register';
	}
	setDone(true);
}
main();

// using qjsc.exe, run:
// cd /path/to/jspp/test
// qjsc.exe -b -o "qjs_bytecode.bin" .\qjs_bytecode.js
