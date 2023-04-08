#pragma once

// Namespace with math from single DIP engine
namespace mth {
    template<class Type>
    class vec2
    {
    public:
        union
        {
            Type X;
            Type U;
        };

        union
        {
            Type Y;
            Type V;
        };

        inline Type& operator[](unsigned int Index)
        {
            switch (Index)
            {
            case 0:
                return X;
            case 1:
                return Y;
            }
            return X;
        }
    };

    template<class Type>
    class vec4
    {
    public:
        union
        {
            Type X;
            Type R;
        };
        union
        {
            Type Y;
            Type G;
        };
        union
        {
            Type Z;
            Type B;
        };
        union
        {
            Type W;
            Type A;
        };

        inline Type& operator[](unsigned int Index)
        {
            switch (Index)
            {
            case 0:
                return X;
            case 1:
                return Y;
            case 2:
                return Z;
            case 3:
                return W;
            }
            return X;
        }
    };

    template<class Type>
    class vec3
    {
    public:
        /* Vector components */
        union
        {
            Type X;
            Type R;
        };

        union
        {
            Type Y;
            Type G;
        };

        union
        {
            Type Z;
            Type B;
        };

        inline Type& operator[](unsigned int Index)
        {
            switch (Index)
            {
            case 0:
                return X;
            case 1:
                return Y;
            case 2:
                return Z;
            }
            return X;
        }
    };

    template<class Type>
    class matr4
    {
    public:
        Type A[4][4];


        inline Type* operator[](unsigned int Index)
        {
            return A[Index];
        }

        inline const Type* operator[](unsigned int Index) const
        {
            return A[Index];
        }
    };
}

using gdr_index = unsigned int;
