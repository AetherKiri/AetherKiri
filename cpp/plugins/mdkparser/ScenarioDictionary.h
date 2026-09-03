/**
 * シナリオスクリプトを構造化した辞書を管理する
 */

#ifndef __SCENARIO_DICTIONARY_H__
#define __SCENARIO_DICTIONARY_H__

#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"
#include "Tag.h"

namespace AetherKiri::MDKParser {

class ScenarioDictionary {
	tjs_int CurrentLine = 0;
	iTJSDispatch2* Lines = nullptr;
	iTJSDispatch2* CurrentLineArray = nullptr;
	tjs_int LineIndex = 0;

public:
	ScenarioDictionary() {
		Lines = TJSCreateArrayObject();
	}
	~ScenarioDictionary() {
		release();
	}
	void release() {
		if( CurrentLineArray ) {
			CurrentLineArray->Release();
			CurrentLineArray = nullptr;
		}
		if( Lines ) {
			Lines->Release();
			Lines = nullptr;
		}
		CurrentLine = 0;
		LineIndex = 0;
	}
	/** シナリオの行配列を生成する */
	void createLines() {
		if( !Lines ) {
			Lines = TJSCreateArrayObject();
		}
	}
	/** 現在の行の配列を生成する */
	void createLineArray() {
		if( !CurrentLineArray ) {
			CurrentLineArray = TJSCreateArrayObject();
			tTJSVariant val( CurrentLineArray, CurrentLineArray );
			setValue( val );
		}
	}
	/** 現在の行に値を設定する */
	void setValue( const tTJSVariant& val ) {
		createLines();
		Lines->PropSetByNum( TJS_MEMBERENSURE, CurrentLine, &val, Lines );
	}
	/** 現在の行に整数を設定する */
	void setValue( tjs_int i ) {
		tTJSVariant tmp( i );
		setValue( tmp );
	}
	/** 現在の行にvoidを設定する */
	void setVoid() {
		tTJSVariant tmp;
		setValue( tmp );
	}
	/** 現在の行にタグを設定する */
	void setTag( Tag& tag ) {
		iTJSDispatch2* dic = tag.getTag();
		if( dic ) {
			tTJSVariant tmp( dic, dic );
			setValue( tmp );
		}
	}
	/** 現在の行配列に値を追加する */
	void addValueToCurrentLine( const tTJSVariant& val ) {
		createLineArray();
		CurrentLineArray->PropSetByNum( TJS_MEMBERENSURE, LineIndex, &val, CurrentLineArray );
		LineIndex++;
	}
	/** 現在の行配列にタグを追加する */
	void addTagToCurrentLine( Tag& tag ) {
		iTJSDispatch2* dic = tag.getTag();
		if( dic ) {
			tTJSVariant tmp( dic, dic );
			addValueToCurrentLine( tmp );
		}
	}
	/** 現在の行を設定する */
	void setCurrentLine( tjs_int line ) {
		if( CurrentLine != line ) {
			CurrentLine = line;
			if( CurrentLineArray ) {
				CurrentLineArray->Release();
				CurrentLineArray = nullptr;
				LineIndex = 0;
			}
		}
	}
	iTJSDispatch2* getArray() { return Lines; }
};


} // namespace AetherKiri::MDKParser

#endif // __SCENARIO_DICTIONARY_H__
