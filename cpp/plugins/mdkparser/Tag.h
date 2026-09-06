/**
 * タグを構成する辞書クラスを管理する
 */
#ifndef __TAG_H__
#define __TAG_H__

#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"
#include "ReservedWord.h"

namespace AetherKiri::MDKParser {

class Tag {
	iTJSDispatch2* dic_ = nullptr;			// dictionary
	iTJSDispatch2* attribute_ = nullptr;	// dictionary
	iTJSDispatch2* parameter_ = nullptr;	// dictionary
	iTJSDispatch2* command_ = nullptr;		// array
	tjs_int command_count_ = 0;

public:
	Tag() {}
	Tag( const tTJSVariantString* name ) {
		dic_ = TJSCreateDictionaryObject();
		tTJSVariant tag( *name );
		dic_->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->tag(), &tag, dic_ );
	}
	Tag( iTJSDispatch2 * dic ) : dic_( dic ) {}
	~Tag() {
		release();
	}
	/** 各要素を開放する */
	void release() {
		if( attribute_ ) {
			attribute_->Release();
			attribute_ = nullptr;
		}
		if( parameter_ ) {
			parameter_->Release();
			parameter_ = nullptr;
		}
		if( command_ ) {
			command_->Release();
			command_ = nullptr;
		}
		if( dic_ ) {
			dic_->Release();
			dic_ = nullptr;
		}
		command_count_ = 0;
	}
	/** 辞書を生成する */
	void createDic() {
		if( !dic_ ) {
			dic_ = TJSCreateDictionaryObject();
		}
	}
	/** 属性を生成する */
	void createAttribute() {
		createDic();
		if( !attribute_ ) {
			attribute_ = TJSCreateDictionaryObject();
			tTJSVariant tmp( attribute_, attribute_ );
			dic_->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->attribute(), &tmp, dic_ );
		}
	}
	/** パラメータを生成する */
	void createParameter() {
		createDic();
		if( !parameter_ ) {
			parameter_ = TJSCreateDictionaryObject();
			tTJSVariant tmp( parameter_, parameter_ );
			dic_->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->parameter(), &tmp, dic_ );
		}
	}
	/** コマンドを生成する */
	void createCommand() {
		createDic();
		if( !command_ ) {
			command_ = TJSCreateArrayObject();
			tTJSVariant tmp( command_, command_ );
			dic_->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->command(), &tmp, dic_ );
		}
	}
	/** 指定した名前で値を設定する */
	void setValue( const tTJSVariantString* name, const tTJSVariant& val ) {
		createDic();
		dic_->PropSetByVS( TJS_MEMBERENSURE, const_cast<tTJSVariantString*>( name ), &val, dic_ );
	}
	/** 指定した名前で文字列を設定する */
	void setText( const tTJSVariantString* name, const ttstr& txt ) {
		tTJSVariant val( txt );
		setValue( name, val );
	}
	/** タグ名を設定する */
	void setTagName( const tTJSVariantString* name ) {
		createDic();
		tTJSVariant val( *name );
		setValue( GetRWord()->tag(), val );
	}
	/** タグ名が設定されているかチェックする */
	bool existTagName() {
		if( dic_ ) {
			tTJSVariant v;
			tjs_error hr = dic_->PropGet( TJS_MEMBERMUSTEXIST, GetRWord()->tag_.c_str(), nullptr, &v, dic_ );
			return ( hr != TJS_E_MEMBERNOTFOUND );
		} else {
			return false;
		}
	}
	/** 属性を設定する
	 * @return true 再設定/false 新規追加
	 */
	bool setAttribute(const tTJSVariantString* name, const tTJSVariant& value ) {
		bool exist = isExistAttribute( *name );
		createAttribute();
		attribute_->PropSetByVS( TJS_MEMBERENSURE, const_cast<tTJSVariantString*>(name), &value, attribute_ );
		return exist;
	}
	/** パラメータを設定する
	 * @return true 再設定/false 新規追加
	 */
	bool setParameter(const tTJSVariantString* name, const tTJSVariant& value ) {
		bool exist = isExistParameter( *name );
		createParameter();
		parameter_->PropSetByVS( TJS_MEMBERENSURE, const_cast<tTJSVariantString*>(name), &value, parameter_ );
		return exist;
	}
	/** ファイルプロパティを属性かパラメータに設定する */
	bool setFileProperty( const tTJSVariantString* name, const tTJSVariantString* file, const tTJSVariantString* prop, bool isparam ) {
		iTJSDispatch2* dic = TJSCreateDictionaryObject();
		tTJSVariant vfile( *file );
		dic->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->file(), &vfile, dic );
		tTJSVariant vprop( *prop );
		dic->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->prop(), &vprop, dic );
		tTJSVariant tmp( dic, dic );
		dic->Release();
		if( isparam ) {
			return setParameter( name, tmp );
		} else {
			return setAttribute( name, tmp );
		}
	}
	/** 参照を属性かパラメータに設定する */
	bool setReference( const tTJSVariantString* name, const tTJSVariantString* ref, bool isparam ) {
		iTJSDispatch2* dic = TJSCreateDictionaryObject();
		tTJSVariant vref( *ref );
		dic->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->ref(), &vref, dic );
		tTJSVariant tmp( dic, dic );
		dic->Release();
		if( isparam ) {
			return setParameter( name, tmp );
		} else {
			return setAttribute( name, tmp );
		}
	}
	/** コマンドを追加する */
	void addCommand( const tTJSVariantString* name ) {
		createCommand();
		tTJSVariant val( *name );
		command_->PropSetByNum( TJS_MEMBERENSURE, command_count_, &val, command_ );
		command_count_++;
	}
	/** 指定された名前の属性が存在するかチェックする */
	bool isExistAttribute( const tjs_char* name ) {
		if( attribute_ ) {
			tTJSVariant v;
			tjs_error hr = attribute_->PropGet( TJS_MEMBERMUSTEXIST, name, nullptr, &v, attribute_ );
			return ( hr != TJS_E_MEMBERNOTFOUND );
		} else {
			return false;
		}
	}
	/** 指定された名前のパラメータが存在するかチェックする */
	bool isExistParameter( const tjs_char* name ) {
		if( parameter_ ) {
			tTJSVariant v;
			tjs_error hr = parameter_->PropGet( TJS_MEMBERMUSTEXIST, name, nullptr, &v, parameter_ );
			return ( hr != TJS_E_MEMBERNOTFOUND );
		} else {
			return false;
		}
	}

	/** タグとなる辞書を取得する */
	iTJSDispatch2* getTag() { return dic_; }
};


} // namespace AetherKiri::MDKParser

#endif // __TAG_H__
