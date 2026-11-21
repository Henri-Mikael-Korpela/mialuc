// TODO Fix this, this is still invalid code
package serialization

Book :: struct ->
    isbn : Str ; #(json_name("isbn"))
    name: Str  ; #(json_name("name"))

serialize_book :: proc(book: Book) Str ->
    result := str_builder_make('{')

    for member in type_info_of(Book).members ->
        for attr in member.attributes ->
            if type_id_of(attr) == type_id_of(json_name) ->
                json_name_attr := cast(type_of(json_name))attr

                // Write struct field name as propery key
                str_builder_append_rune(result, '"')
                str_builder_append_str(result, json_name_attr.name)
                str_builder_append_rune(result, '":')

                // Write struct field value as property value
                if member.type_id == type_id_of(Str) ->
                    value : Str
                    // TODO Fix this mess
                    mem.copy(member.offset_in_bytes)
                    str_builder_append_str(result, )
                else ->
                    // FAIL

    str_builder_append_rune('}')
    return str_builder_to_str(result)

main :: proc() Int ->
    book_serialized := serialize_book(Book {
        isbn: "12345",
        name: "My book",
    })
    return 0